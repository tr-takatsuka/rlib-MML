
//#ifndef _MSC_VER
//#include <bits/stdc++.h>
//#endif

#include <cstdint>
#include <istream>
#include <iostream>
#include <math.h>

#include "./Smf.h"

using namespace rlib;
using namespace rlib::midi;

class Smf::Inner {
public:
#pragma pack( push )
#pragma pack( 1 )

	struct HeaderChunk {
		std::array<uint8_t, 4>	MThd = { 'M', 'T', 'h', 'd' };	// MThd
		uint32_t				dataLength = 6;					// データ長(6固定)
		uint16_t				format = 0;						// フォーマット
		uint16_t				trackCount = 0;					// トラック数
		uint16_t				division = 0;					// 時間単位
	};

	struct TrackChunk {
		std::array<uint8_t, 4>	MTrk = { 'M','T','r','k' };	// MTrk
		uint32_t				dataLength = 0;				// データ長
	};

#pragma pack( pop )

	// エンディアン変更
	template <typename T> static void changeEndian(T& t) {
		std::reverse(reinterpret_cast<uint8_t*>(&t), reinterpret_cast<uint8_t*>(&t) + sizeof(t));
	}

	template <typename T> static T read(std::istream& is) {
		typename std::remove_const<T>::type buf;
		if (is.read(reinterpret_cast<char*>(&buf), sizeof(buf)).gcount() < sizeof(buf)) {
			throw std::runtime_error("size error");
		}
		return buf;
	}

};

std::vector<uint8_t> Smf::getFileImage() const
{
	std::list<std::vector<uint8_t>> trackData;

	for (auto& track : tracks) {
		std::vector<uint8_t> v;

		size_t position = 0;		// 現在位置

		auto fEvent = [&](const Event& event) {

			{// DeltaTime
				assert(event.position >= position);
				const size_t deltaTime = event.position - position;		// DeltaTime
				position = event.position;								// 現在位置更新
				std::vector<uint8_t> s = midi::inner::getVariableValue(deltaTime);
				v.insert(v.end(), std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
			}

			{
				std::vector<uint8_t> t = event.event->midiMessage();
				v.insert(v.end(), std::make_move_iterator(t.begin()), std::make_move_iterator(t.end()));
			}

		};

		for (auto& event : track.events) {
			fEvent(event);
		}

		// EndOfTrackがなければ付ける
		[&] {
			if (auto i = track.events.rbegin(); i != track.events.rend()) {		// 末尾が EndOfTrack ではないなら
				if (auto meta = std::dynamic_pointer_cast<const midi::EventMeta>(i->event)) {
					if (meta->type == midi::EventMeta::Type::endOfTrack) {
						return;
					}
				}
			}
			Event e(position, std::make_shared<midi::EventMeta>(midi::EventMeta::createEndOfTrack()));
			fEvent(e);
		}();

		{// TrackChunk を先頭に挿入
			Inner::TrackChunk trackChunk;
			trackChunk.dataLength = static_cast<uint32_t>(v.size());
			Inner::changeEndian(trackChunk.dataLength);
			v.insert(v.begin(),
				reinterpret_cast<const uint8_t*>(&trackChunk),
				reinterpret_cast<const uint8_t*>(&trackChunk) + sizeof(trackChunk));
		}

		trackData.emplace_back(std::move(v));
	}

	const Inner::HeaderChunk headerChunk = [&] {
		Inner::HeaderChunk h;
		h.trackCount = static_cast<uint16_t>(trackData.size());
		h.format = h.trackCount > 1 ? 1 : 0;
		h.division = timeBase;
		// エンディアン変更
		Inner::changeEndian(h.dataLength);
		Inner::changeEndian(h.format);
		Inner::changeEndian(h.trackCount);
		Inner::changeEndian(h.division);
		return h;
	}();

	std::vector<uint8_t> result(
		reinterpret_cast<const uint8_t*>(&headerChunk),
		reinterpret_cast<const uint8_t*>(&headerChunk) + sizeof(headerChunk));

	for (auto& i : trackData) {
		result.insert(result.end(), std::make_move_iterator(i.begin()), std::make_move_iterator(i.end()));
	}

	return result;
}

Smf Smf::fromStream(std::istream& is)
{
	using namespace midi;

	Smf smf;

	const auto headerChunk = [&is]() {
		Inner::HeaderChunk c = Inner::read<decltype(c)>(is);
		if (c.MThd != Inner::HeaderChunk().MThd) {
			throw std::runtime_error("MThd chunk error");
		}
		Inner::changeEndian(c.dataLength);
		Inner::changeEndian(c.format);
		Inner::changeEndian(c.trackCount);
		Inner::changeEndian(c.division);
		return c;
	}();

	smf.timeBase = headerChunk.division;

	// トラックチャンク
	for (size_t i = 0; i < headerChunk.trackCount; i++) {
		const auto trackChunk = [&is]() {
			Inner::TrackChunk c = Inner::read<decltype(c)>(is);
			if (c.MTrk != Inner::TrackChunk().MTrk) {
				throw std::runtime_error("MTrk chunk error");
			}
			Inner::changeEndian(c.dataLength);
			return c;
		}();

		Smf::Track track;

		std::uint64_t currentPosition = 0;		// 現在位置
		uint8_t beforeStatus = 0;
		for (const auto beginPosition = is.tellg(); is.tellg() - beginPosition < trackChunk.dataLength; ) {
			auto readVariableValue = [&is] {					// 可変長数値を取得
				return inner::readVariableValue([&] {return Inner::read<uint8_t>(is); });
			};

			currentPosition += readVariableValue();		// 現在位置 += デルタタイム

			// status 読み込み
			const auto status = [&] {
				const uint8_t status = Inner::read<decltype(status)>(is);
				if (!(status & 0x80)) {					// status 省略なら直前値を採用
					is.seekg(-1, std::ios::cur);		// 位置を戻す
					return beforeStatus;
				}
				beforeStatus = status;
				return status;
			}();

			switch (status & 0xf0) {
			case EventNoteOff::statusByte: {
				const std::array<uint8_t, 2> a = Inner::read<decltype(a)>(is);
				track.events.emplace(currentPosition, std::make_shared<EventNoteOff>(status & 0xf, a[0] & 0x7f, a[1] & 0x7f));
				break;
			}
			case EventNoteOn::statusByte: {
				const std::array<uint8_t, 2> a = Inner::read<decltype(a)>(is);
				track.events.emplace(currentPosition, std::make_shared<EventNoteOn>(status & 0xf, a[0] & 0x7f, a[1] & 0x7f));
				break;
			}
			case EventPolyphonicKeyPressure::statusByte: {
				const std::array<uint8_t, 2> a = Inner::read<decltype(a)>(is);
				track.events.emplace(currentPosition, std::make_shared<EventPolyphonicKeyPressure>(status & 0xf, a[0] & 0x7f, a[1] & 0x7f));
				break;
			}
			case EventControlChange::statusByte: {
				const std::array<uint8_t, 2> a = Inner::read<decltype(a)>(is);
				track.events.emplace(currentPosition, std::make_shared<EventControlChange>(status & 0xf, a[0] & 0x7f, a[1] & 0x7f));
				break;
			}
			case EventProgramChange::statusByte: {
				const uint8_t n = Inner::read<decltype(n)>(is);
				track.events.emplace(currentPosition, std::make_shared<EventProgramChange>(status & 0xf, n & 0x7f));
				break;
			}
			case EventPitchBend::statusByte: {
				const std::array<uint8_t, 2> a = Inner::read<decltype(a)>(is);
				const auto n = ((a[0] & 0x7f) + (a[1] & 0x7f) * 0x80) - 8192;
				track.events.emplace(currentPosition, std::make_shared<EventPitchBend>(status & 0xf, n));
				break;
			}
			case EventChannelPressure::statusByte: {
				const uint8_t n = Inner::read<decltype(n)>(is);
				track.events.emplace(currentPosition, std::make_shared<EventChannelPressure>(status & 0xf, n & 0x7f));
				break;
			}
			default:
				switch (status) {
				case EventExclusive::statusByte: {
					const auto size = readVariableValue();		// データ長
					std::vector<uint8_t> data;
					for (auto i = 0; i < size; i++) {
						const uint8_t n = Inner::read<decltype(n)>(is);
						if (n == 0xf7) {				// EOX(終了コード)
							if(i != size - 1){				// ヘンなところで終わった？
								throw std::runtime_error("exclusive size error");
							}
							break;
						}
						if ((n & 0x80) != 0) {
							// あるべきハズのEOX(0xf7)が無い(が、エラーにはせず続行)
							std::clog << "[warning] exclusive data error. EOX(0xf7) is missing." << std::endl;
							break;
						}
						data.push_back(n);
					}
					track.events.emplace(currentPosition, std::make_shared<EventExclusive>(std::move(data)));
					break;
				}
				case EventMeta::statusByte: {
					const uint8_t type = Inner::read<decltype(type)>(is);					// イベントタイプ
					const auto len = readVariableValue();									// データ長
					std::vector<uint8_t> data(len);
					if (is.read(reinterpret_cast<char*>(data.data()), data.size()).gcount() < static_cast<std::intmax_t>(data.size())) {
						throw std::runtime_error("size error");
					}
					track.events.emplace(currentPosition, std::make_shared<EventMeta>(static_cast<EventMeta::Type>(type), std::move(data)));
					break;
				}
				default:
					assert(false);
					break;
				}
			}

		}
		smf.tracks.emplace_back(std::move(track));

	}
	return smf;

}

Smf Smf::convertTimebase(const Smf& smf, int timeBase) {
	Smf dst;
	dst.timeBase = timeBase;
	const auto mul = static_cast<double>(dst.timeBase) / smf.timeBase;
	for (auto& track : smf.tracks) {
		Track dstTrack;
		for (auto& event : track.events) {
			const auto dstPos = static_cast<decltype(Event::position)>(std::round(event.position * mul));
			dstTrack.events.emplace(dstPos, event.event);
		}
		dst.tracks.push_back(dstTrack);
	}
	return dst;
}
