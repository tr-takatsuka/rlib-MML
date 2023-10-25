#pragma once

#include "MidiEvent.h"
#include "Smf.h"
#include "TempoList.h"
//#include "MmlCompiler.h"

#include "../stringformat/StringFormat.h"

namespace rlib::sequencer {

	inline std::string smfToMml(const midi::Smf& smf) {
		using Smf = midi::Smf;
		auto smf2 = Smf::convertTimebase(smf, 480);		// timebaseを480に

		std::string result;

		auto trackNo = 0;
		for (auto itTrack = smf2.tracks.begin(); itTrack != smf2.tracks.end(); itTrack++, trackNo++) {
			auto& track = *itTrack;

			// チャンネル毎のデータにする
			const auto channels = [&] {
				std::multiset<Smf::Event, Smf::Event::Less> meta;
				std::map<int, decltype(meta)>	channels;

				for (auto& event : track.events) {
					if (auto e = std::dynamic_pointer_cast<const midi::EventCh>(event.event)) {
						channels[e->channel].insert(event);
					} else {
						meta.insert(event);
					}
				}
				{// metaイベントを一番若いチャンネルの列に
					auto& ch = [&]()-> decltype(meta)& {
						if (auto i = channels.begin(); i != channels.end()) {
							return i->second;
						}
						return channels[0];
					}();
					for (auto& e : meta) {
						ch.insert(ch.lower_bound(e.position), e);	// 等価の列の先頭に挿入
					}
				}
				return channels;
			}();
			typedef decltype(channels.at(0).begin()) ItEvents;


			for (auto& ch : channels) {
				const auto channel = ch.first;

				struct {
					int		note = -1;
					size_t	position = 0;	// 現在の位置
					int		velocity = -1;
					bool	noteUnmove = false;				// 音符で位置を更新しないモード
				}state;

				result += string::format(u8"\ncreatePort(name:tr%dch%02d, channel:%d)", trackNo, channel + 1, channel + 1);
				result += string::format(u8"\nport(tr%dch%02d)", trackNo, channel + 1);
				result += string::format(u8" l8 ");

				// 音長 文字列取得
				const auto getLengthText = [](size_t len)->std::string {
					if (len <= 0) return "!0";
					std::string r;
					const auto lenTo = [&r](size_t len,bool needTime)->size_t {
						if (needTime) r += "^";
						switch (len) {
						case 60:		r += "32";	return 60;
						case 60 + 30:	r += "32.";	return 60 + 30;
						case 120:		r += "16";	return 120;
						case 120 + 60:	r += "16.";	return 120 + 60;
						case 240 + 120:	r += ".";	return 240 + 120;
						case 120 / 3:	r += "48";	return 120 / 3;
						case 240 / 3:	r += "24";	return 240 / 3;
						case 480 / 3:	r += "12";	return 480 / 3;
						}
						if (len >= 240) {
							return 240;
						}
						r += string::format(u8"!%d", len);
						return len;
					};
					for (int i = 0; len > 0; i++) {
						auto n = lenTo(len, i > 0);
						len -= n;
					}
					return r;
				};

				const std::map<std::type_index, std::function<void(const ItEvents&)> > map = {
					{typeid(midi::EventNoteOn), [&](const ItEvents& it) {
						auto& e = static_cast<const midi::EventNoteOn&>(*it->event);
						if (e.velocity <= 0) return;	// NoteOff扱いのものは無視

						std::string mml;

						// velocity
						if (state.velocity != e.velocity) {
							state.velocity = e.velocity;
							mml += string::format(u8"v%d", state.velocity);
						}

						// ノートオフか？
						const auto isNoteOff = [&](const auto& spEvent) {
							if (auto spNote = std::dynamic_pointer_cast<const midi::EventNote>(spEvent)) {
								if (spNote->note == e.note) {
									return spNote->velocity <= 0 || std::dynamic_pointer_cast<const midi::EventNoteOff>(spEvent);
								}
							}
							return false;
						};

						// 音の長さ算出
						const size_t len = [&]()->decltype(len) {
							auto r = std::find_if(it, ch.second.end(), [&](auto& x) {
								return isNoteOff(x.event);
							});
							if (r == ch.second.end()) {
								return 480 / 2;				// ノートオフがなければ8分音符
							}
							return r->position - it->position;
						}();

						static const std::vector<std::string> noteTable = {
							"c","c+","d", "d+", "e", "f", "f+", "g", "g+", "a", "a+", "b"
						};
						const auto& note = noteTable[e.note % noteTable.size()];
						const int oct = e.note / static_cast<int>(noteTable.size());

						if (state.note < 0) {
							mml += string::format(u8" o%d ", oct - 2);
							state.note = e.note;
						}

						const int diffOct = oct - ( state.note / static_cast<int>(noteTable.size()) );
						for (int i = 0; i < std::abs(diffOct); i++) {
							mml += diffOct > 0 ? "<" : ">";
						}
						mml += string::format(u8"%s", note);
						state.note = e.note;

						// 音長
						mml += getLengthText(len);

						{// ノートオフより先のイベントがあるか？
							auto i = it;
							i++;
							if (isNoteOff(i->event) || i->position >= it->position + len) {	// ない
								if(state.noteUnmove){
									mml = "'" + mml;				// 通常モードに変更
									state.noteUnmove = false;
								}
							} else {													// ある
								if (!state.noteUnmove) {
									mml = "'" + mml;				// Noteで現在位置を進めないモードに変更
									state.noteUnmove = true;
								}
							}
						}

						if (!state.noteUnmove) {
							state.position += len;
						}

						result += mml;
					}},

					{typeid(midi::EventControlChange), [&](const ItEvents& it) {
						auto& e = static_cast<const midi::EventControlChange&>(*it->event);
						switch (e.type) {
						case midi::EventControlChange::Type::volume:
							result += string::format(u8"V(%s)", static_cast<int>(e.value));
							break;
						case midi::EventControlChange::Type::pan:
							result += string::format(u8"pan(%s)", static_cast<int>(e.value));
							break;
						default:
							break;
						}
					}},

					{typeid(midi::EventProgramChange), [&](const ItEvents& it) {
						auto& e = static_cast<const midi::EventProgramChange&>(*it->event);
						result += string::format(u8"@%d", static_cast<int>(e.programNo));
					}},

					{typeid(midi::EventMeta), [&](const ItEvents& it) {
						auto& e = static_cast<const midi::EventMeta&>(*it->event);
						switch (e.type) {
						case midi::EventMeta::Type::tempo:
							result += string::format(u8"t%s", e.getTempo());
							break;
						default:
							break;
						}
					}},
				};

				for (auto it = ch.second.begin(); it != ch.second.end(); it++) {

					// delta time (休符)
					if (const size_t len = it->position - state.position; len > 0) {
						result += string::format(u8"r%s", getLengthText(len));
						state.position = it->position;
					}

					if (auto i = map.find(typeid(*it->event)); i != map.end()) {
						(i->second)(it);
					}// else assert(false);
				}

			}

		}

		return result;
	}

}
