
#ifndef _MSC_VER
#include <bits/stdc++.h>
#endif

#include "./Smf.h"
#include "../stringformat/StringFormat.h"

using namespace rlib;
using namespace rlib::sequencer;

class Smf::Inner {
public:
#pragma pack( push )
#pragma pack( 1 )

	struct HeaderChunk {
		uint8_t	MThd[4] = { 'M','T','h','d' };	// MThd
		uint32_t	dataLength = 6;				// データ長(6固定)
		uint16_t	format = 0;					// フォーマット
		uint16_t	trackCount = 0;				// トラック数
		uint16_t	division = 0;				// 時間単位
	};

	struct TrackChunk {
		uint8_t	MTrk[4] = { 'M','T','r','k' };	// MTrk
		uint32_t	dataLength = 0;				// データ長
	};

	union VariableValue {
		struct {
			uint64_t	b0 : 7;
			uint64_t	b1 : 7;
			uint64_t	b2 : 7;
			uint64_t	b3 : 7;
			uint64_t	b4 : 7;
			uint64_t	b5 : 7;
			uint64_t	b6 : 7;
			uint64_t	b7 : 7;
			uint64_t	reserved : 8;
		};
		uint64_t	value = 0;
	};
	union VariableByte {
		struct {
			uint8_t	b7 : 7;
			uint8_t	flag : 1;
		};
		uint8_t		value;

		VariableByte(uint8_t b7_, bool flag_)
			:b7(b7_)
			, flag(flag_)
		{}
	};
#pragma pack( pop )

	// エンディアン変更
	template <typename T> static void changeEndian(T& t) {
		size_t size = sizeof(t);
		auto p = reinterpret_cast<std::uint8_t*>(&t);
		for (size_t i = 0; i < size / 2; i++) {
			size_t j = size - 1 - i;
			std::uint8_t n = p[i];
			p[i] = p[j];
			p[j] = n;
		}
	}

	static std::vector<uint8_t> getVariableValue(unsigned long long n) {
		std::vector<uint8_t> v;
		VariableValue vv;
		vv.value = n;
		if (vv.b7) v.push_back(VariableByte(vv.b7, true).value);
		if (vv.b6 || !v.empty()) v.push_back(VariableByte(vv.b6, true).value);
		if (vv.b5 || !v.empty()) v.push_back(VariableByte(vv.b5, true).value);
		if (vv.b4 || !v.empty()) v.push_back(VariableByte(vv.b4, true).value);
		if (vv.b3 || !v.empty()) v.push_back(VariableByte(vv.b3, true).value);
		if (vv.b2 || !v.empty()) v.push_back(VariableByte(vv.b2, true).value);
		if (vv.b1 || !v.empty()) v.push_back(VariableByte(vv.b1, true).value);
		v.push_back(VariableByte(vv.b0, false).value);
		return v;
	}
};

Smf::EventMeta Smf::EventMeta::createTempo(size_t position, double tempo) {
	EventMeta e(position);
	e.type = Type::tempo;
	union {
		unsigned char	buf[4] = { 0, };
		uint32_t	tempo;
	}t;
	assert(sizeof(t) == 4);
	t.tempo = static_cast<uint32_t>(60000000 / tempo);		// 4分音符の時間(microsec)
	Inner::changeEndian(t.tempo);
	e.data = { t.buf[1], t.buf[2], t.buf[3] };		// 3バイト
	return e;
}

Smf::EventMeta Smf::EventMeta::createEndOfTrack(size_t position) {
	EventMeta e(position);
	e.type = Type::endOfTrack;
	return e;
}

std::vector<uint8_t> Smf::getFileImage() const
{
	std::list<std::vector<uint8_t>> trackData;

	for (auto& track : tracks) {
		std::vector<uint8_t> v;

		size_t position = 0;		// 現在位置

		auto fEvent = [&](const Event& event) {

			static const std::map<std::type_index, std::function<std::vector<uint8_t>(const Smf::Event&)>> m = {
				{typeid(Smf::EventNoteOn), [](const Smf::Event& event) {			// EventNoteOn
					auto& e = static_cast<const Smf::EventNoteOn&>(event);
					return std::vector<uint8_t>{
						static_cast<uint8_t>(0x90 | (e.channel & 0xf)),
						static_cast<uint8_t>(e.note & 0x7f),
						static_cast<uint8_t>(e.velocity & 0x7f),
					};
				}},
				{typeid(Smf::EventNoteOff), [](const Smf::Event& event) {			// EventNoteOff
					auto& e = static_cast<const Smf::EventNoteOff&>(event);
					return std::vector<uint8_t>{
						static_cast<uint8_t>(0x80 | (e.channel & 0xf)),
						static_cast<uint8_t>(e.note & 0x7f),
						static_cast<uint8_t>(e.velocity & 0x7f),
					};
				}},
				{typeid(Smf::EventProgramChange), [](const Smf::Event& event) {		// EventProgramChange
					auto& e = static_cast<const Smf::EventProgramChange&>(event);
					return std::vector<uint8_t>{
						static_cast<uint8_t>(0xc0 | (e.channel & 0xf)),
						static_cast<uint8_t>(e.programNo & 0x7f),
					};
				}},
				{typeid(Smf::EventMeta), [](const Smf::Event& event) {				// EventMeta
					auto& e = static_cast<const Smf::EventMeta&>(event);
					std::vector<uint8_t> v{
						0xff,
						static_cast<uint8_t>(e.type),
					};
					{// データサイズ
						std::vector<uint8_t> s = Inner::getVariableValue(e.data.size());
						v.insert(v.end(), std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
					}
					v.insert(v.end(), e.data.begin(), e.data.end());	// データ
					return v;
				}},
			};

			{// DeltaTime
				assert(event.position >= position);
				const size_t deltaTime = event.position - position;		// DeltaTime
				position = event.position;								// 現在位置更新
				std::vector<uint8_t> s = Inner::getVariableValue(deltaTime);
				v.insert(v.end(), std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
			}

			if (auto i = m.find(typeid(event)); i != m.end()) {
				std::vector<uint8_t> t = (i->second)(event);
				v.insert(v.end(), std::make_move_iterator(t.begin()), std::make_move_iterator(t.end()));
			} else assert(false);
		};

		for (auto& event : track.events) {
			fEvent(*event);
		}

		// EndOfTrackがなければ付ける
		[&] {
			if (auto i = track.events.rbegin(); i != track.events.rend()) {		// 末尾が EndOfTrack ではないなら
				if (auto meta = std::static_pointer_cast<const EventMeta>(*i)) {
					if (meta->type == EventMeta::Type::endOfTrack) {
						return;
					}
				}
			}
			fEvent(EventMeta::createEndOfTrack(position));
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

	const Inner::HeaderChunk headerChunk = [&]{
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

