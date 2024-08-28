#pragma once

#include "MidiEvent.h"
#include "Smf.h"
#include "TempoList.h"
#include "MmlCompiler.h"

#include "../stringformat/StringFormat.h"

namespace rlib::sequencer {

	inline std::string smfToMml(const midi::Smf& smf) {
		using Smf = midi::Smf;

		const auto makeMml = [](const std::string& name, const std::string& instrument, int channel, const Smf::Events& events) {

			struct {
				int		note = -1;
				size_t	position = 0;	// 現在の位置
				int		velocity = -1;
				bool	noteUnmove = false;				// 音符で位置を更新しないモード
			}state;

			std::vector<std::string> mmls(1);

			// 音長 文字列取得
			const auto getLengthText = [](size_t len, size_t position, bool movePosition)->std::vector<std::string> {
				if (len <= 0) return { "!0" };
				std::vector<std::string> result(1);
				auto current = position;
				for (int i = 0; len > 0; i++) {
					const auto lenTo = [&](size_t targetLen) {

						const auto add = [&](const std::string& code, size_t codeLen = 0) {
							if (i > 0) *result.rbegin() += "^";
							*result.rbegin() += code;

							auto before = current;
							current += codeLen ? codeLen : targetLen;
							len -= codeLen ? codeLen : targetLen;

							if (movePosition) {
								constexpr size_t delim = 1920 * 4;	// n小節で改行
								if (before / delim != current / delim) {
									result.push_back("");
								}
							}

						};

						// 付点n分音符の式 
						constexpr const auto dotLen = [](int note, int dot)->int {
							int r = note, half = note;
							for (int i = 0; i < dot; i++) {
								half /= 2;
								r += half;
							}
							return r;
						};

						switch (targetLen) {
						case 30:					add("64");		return;
						case 120 / 3:				add("48");		return;
						case 60:					add("32");		return;
						case dotLen(60, 1):			add("32.");		return;
						case 240 / 3:				add("24");		return;
						case dotLen(240 / 3, 2):	add("24..");	return;
						case 120:					add("16");		return;
						case dotLen(120, 1):		add("16.");		return;
						case dotLen(120, 2):		add("16..");	return;
						case 480 / 3:				add("12");		return;
						case dotLen(480 / 3, 2):	add("12..");	return;
						case 240:					add("");		return;
						case dotLen(240, 1):		add(".");		return;
						case dotLen(240, 2):		add("..");		return;
						case dotLen(240, 3):		add("...");		return;
						case 480:					add("^");		return;
						case dotLen(480, 1):		add("4.");		return;
						case dotLen(480, 2):		add("4..");		return;
						case dotLen(480, 3):		add("4...");	return;
						case 960:					add("2");		return;
						case dotLen(960,1):			add("2.");		return;
						case dotLen(960,2):			add("2..");		return;
						case dotLen(960,3):			add("2...");	return;
						case 960 + 240:				add("2^");		return;
						case 1920:					add("1");		return;
						default:	break;
						}
						if (targetLen > 1920) {
							add("1", 1920);
							return;
						}
						add(string::format(u8"!%d", targetLen));
					};

					if (auto mod = current % 240) {	// 区切り位置の余り
						auto n = 240 - mod;
						if (len >= n) {
							lenTo(n);
							continue;
						}
					}

					lenTo(len);
				}
				return result;
			};

			const std::map<std::type_index, std::function<void(const Smf::Events::iterator&)> > map = {
				{typeid(midi::EventNoteOn), [&](const Smf::Events::iterator& it) {
					auto& e = static_cast<const midi::EventNoteOn&>(*it->event);
					if (e.velocity <= 0) return;	// NoteOff扱いのものは無視

					std::string r;

					// velocity
					if (state.velocity != e.velocity) {
						state.velocity = e.velocity;
						r += string::format(u8"v%d", state.velocity);
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
						auto r = std::find_if(it, events.end(), [&](auto& x) {
							return isNoteOff(x.event);
						});
						if (r == events.end()) {
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
						r += string::format(u8" o%d ", oct - 2);
						state.note = e.note;
					}

					const int diffOct = oct - (state.note / static_cast<int>(noteTable.size()));
					for (int i = 0; i < std::abs(diffOct); i++) {
						r += diffOct > 0 ? "<" : ">";
					}
					r += string::format(u8"%s", note);
					state.note = e.note;

					// ノートオフより先のイベントがあるか？(重なるか？)
					const bool overlap = [&] {
						auto i = it;
						i++;
						if (isNoteOff(i->event) || i->position >= it->position + len) {	// ない?
							return false;
						}
						return true;
					}();

					// 音長
					auto vLen = getLengthText(len, state.position, !overlap);
					r += vLen[0];

					// noteUnmove 切り替え
					if (!overlap) {	// 重なりナシ
						if (state.noteUnmove) {
							r = "'" + r;				// 通常モードに変更
							state.noteUnmove = false;
						}
					} else {		// 重なりアリ
						if (!state.noteUnmove) {
							r = "'" + r;				// Noteで現在位置を進めないモードに変更
							state.noteUnmove = true;
						}
					}

					if (!state.noteUnmove) {
						state.position += len;
					}

					*mmls.rbegin() += r;
					for (auto i = 1; i < vLen.size(); i++) {
						mmls.emplace_back(std::move(vLen[i]));
					}

				}},

				{typeid(midi::EventControlChange), [&](const Smf::Events::iterator& it) {
					auto& e = static_cast<const midi::EventControlChange&>(*it->event);
					switch (e.type) {
					case midi::EventControlChange::Type::volume:
						*mmls.rbegin() += string::format(u8"V(%s)", static_cast<int>(e.value));
						break;
					case midi::EventControlChange::Type::pan:
						*mmls.rbegin() += string::format(u8"Pan(%s)", static_cast<int>(e.value));
						break;
					default:
						*mmls.rbegin() += string::format(u8"CC(%s,%s)", static_cast<int>(e.type), static_cast<int>(e.value));
						break;
					}
				}},

				{typeid(midi::EventProgramChange), [&](const Smf::Events::iterator& it) {
					auto& e = static_cast<const midi::EventProgramChange&>(*it->event);
					*mmls.rbegin() += string::format(u8"@%d", static_cast<int>(e.programNo));
				}},

				{typeid(midi::EventPitchBend), [&](const Smf::Events::iterator& it) {
					auto& e = static_cast<const midi::EventPitchBend&>(*it->event);
					*mmls.rbegin() += string::format(u8"PitchBend(%d)", static_cast<int>(e.pitchBend));
				}},

				{typeid(midi::EventMeta), [&](const Smf::Events::iterator& it) {
					auto& e = static_cast<const midi::EventMeta&>(*it->event);
					switch (e.type) {
					case midi::EventMeta::Type::tempo:
						*mmls.rbegin() += string::format(u8"t%s", e.getTempo());
						break;
					default:
						break;
					}
				}},
			};

			for (auto it = events.begin(); it != events.end(); it++) {

				// delta time (休符)
				if (const size_t len = it->position - state.position; len > 0) {
					auto vLen = getLengthText(len, state.position, true);
					*mmls.rbegin() += string::format(u8"r%s", vLen[0]);
					for (auto i = 1; i < vLen.size(); i++) {
						mmls.emplace_back(std::move(vLen[i]));
					}
					state.position = it->position;
				}
				const auto& ev = *it->event;
				if (auto i = map.find(typeid(ev)); i != map.end()) {
					(i->second)(it);
				}// else assert(false);
			}

			std::string result;

			const auto instrumentText = instrument.empty() ? "" : string::format(u8"instrument:%s, ", instrument);
			result += string::format(u8"\nCreatePort(name:%s, %schannel:%d)", name, instrumentText, channel + 1, channel + 1);
			result += string::format(u8"\nPort(%s)", name, channel + 1);
			result += string::format(u8" l8 ");

			for (const auto& mml : mmls) {
				result += mml + "\n";
			}

			return result;
		};

		auto smf2 = Smf::convertTimebase(smf, 480);		// timebaseを480に

		using MapEvents = std::map<int, Smf::Events>;	// <ch,evnets>

		struct SmfTrack {
			std::string		instrumentName;
			std::string		sequenceName;
			MapEvents		mapEvents;		// <ch,evnets>
		};

		struct TrackKey {
			std::string		instrumentName = "";
			std::string		sequenceName = "noname";
			bool operator<(const TrackKey& b) const {
				if (instrumentName != b.instrumentName) return instrumentName < b.instrumentName;
				return sequenceName < b.sequenceName;
			}
		};


		std::map<TrackKey, MapEvents> mapTrack = [](const midi::Smf& smf) {

			std::map<TrackKey, MapEvents> resultMapTrack;

			constexpr auto metaChannel = 999;

			for (auto& track : smf.tracks) {
				TrackKey trackKey;
				for (auto& event : track.events) {
					if (auto e = std::dynamic_pointer_cast<const midi::EventCh>(event.event)) {
						resultMapTrack[trackKey][e->channel].insert(event);
					} else if (auto e = std::dynamic_pointer_cast<const midi::EventMeta>(event.event)) {

						const auto name = [](const std::string &s) {
							// 文字列 trim 全角スペース対応
							const auto stringTrim = [](const std::string& s)->std::string {
								std::smatch m;
								static const std::regex re(u8R"(^(\s|　)*([\s\S]+?)(\s|　)*$)");
								if (std::regex_search(s, m, re)) {
									if (auto sm = m[2]; sm.matched) {
										return sm.str();
									}
								}
								return "";
							};
							auto name = stringTrim(s);
							return name.empty() ? boost::optional<std::string>() : boost::optional<std::string>(name);
						};

						switch (e->type) {
						case midi::EventMeta::Type::sequenceName:
							trackKey.sequenceName = name(e->getText()).value_or(TrackKey().sequenceName);
							break;
						case midi::EventMeta::Type::instrumentName:
							trackKey.instrumentName = name(e->getText()).value_or(TrackKey().instrumentName);
							break;
						default:
							resultMapTrack[trackKey][metaChannel].insert(event);
							break;
						}
					} else if (auto e = std::dynamic_pointer_cast<const midi::EventExclusive>(event.event)) {
						resultMapTrack[trackKey][metaChannel].insert(event);
					} else {
						assert(false);
					}
				}

				// meta用トラックは一番若いチャンネルの列に
				for (auto& i : resultMapTrack) {
					if (auto j = i.second.find(metaChannel); j != i.second.end()) {
						Smf::Events& dstEvents = i.second.size() <= 1 ? (i.second)[0] : i.second.begin()->second;
						auto& events = j->second;
						for (auto k = events.rbegin(); k != events.rend(); k++) {
							dstEvents.insert(dstEvents.lower_bound(k->position), *k);
						}
						i.second.erase(metaChannel);
					}
				}

			}
			return resultMapTrack;
		}(smf2);

		std::string result;

		for (auto& iMapTrack: mapTrack) {
			const auto sequenceName = iMapTrack.first.sequenceName;
			const auto needInstrumentName = [&]{	// 名前に InstrumentName を含める必要アリ（含めないと重複する）
				int count = 0;
				for (auto& i : mapTrack) {
					if(i.first.sequenceName == sequenceName){
						if (++count >= 2) return true;
					}
				}
				return false;
			}();
			for (auto& iMapEvents : iMapTrack.second) {
				const int channel = iMapEvents.first;
				const auto needChannelName = iMapTrack.second.size() > 1;	// 名前に Cannel を含める必要アリ（含めないと重複する）

				const auto ensureName = [](const std::string &name) {
					const auto isValidName = [](const std::string& s) {
						auto r = MmlCompiler::Util::parseWord(s.begin(), s.end());
						if (r && r->word) {
							if (std::holds_alternative<std::pair<std::string::const_iterator, std::string::const_iterator>>(*r->word)) {
								return r->next == s.end();
							}
						}
						return false;
					};
					if (isValidName(name)) return name;
					auto t = string::format(u8R"("%s")",name);	// ダメなら " で括る
					if (isValidName(t)) return t;

					for (int i = 0; i < 1000; i++) {
						auto t = string::format(u8R"(R"t%d(%s)t%d")", i, name, i);	// それでもダメなら R"t?(○○)t?" で括る
						if (isValidName(t)) return t;
					}
					return string::format(u8R"(R"t_(%s)t_")", name);	// 最後は R"t_(○○)t_" で諦める
				};

				const auto trackName = [&] {
					std::string s(sequenceName);
					if (needInstrumentName) {
						s += "-" + iMapTrack.first.instrumentName;
					}
					if (needChannelName) {
						s += string::format("-%02d", channel + 1);
					}
					return ensureName(s);
				}();

				const auto instrument = [&] {
					std::string s(iMapTrack.first.instrumentName);
					if (s.empty()) return s;
					return ensureName(s);
				}();

				Smf::Events& events = iMapEvents.second;

				const auto mml = makeMml(trackName, instrument, channel, events);
				result += mml;

			}
		}


#if 0
		auto mapTracks = [&] {
			std::list<SmfTrack> smfTracks = [&] {
				std::list<SmfTrack> smfTracks;
				for (auto& track : smf2.tracks) {
					SmfTrack smfTrack;
					Smf::Events metaEvents;
					for (auto& event : track.events) {
						if (auto e = std::dynamic_pointer_cast<const midi::EventCh>(event.event)) {
							smfTrack.mapEvents[e->channel].insert(event);
						} else if (auto e = std::dynamic_pointer_cast<const midi::EventMeta>(event.event)) {
							metaEvents.insert(event);
							switch (e->type) {
							case midi::EventMeta::Type::sequenceName:
								if (smfTrack.sequenceName.empty()) smfTrack.sequenceName = e->getText();
								break;
							case midi::EventMeta::Type::instrumentName:
								if (smfTrack.instrumentName.empty()) smfTrack.instrumentName = e->getText();
								break;
							default:
								break;
							}
						} else if (auto e = std::dynamic_pointer_cast<const midi::EventExclusive>(event.event)) {
							metaEvents.insert(event);
						} else {
							assert(false);
						}
					}

					// metaイベントを一番若いチャンネルの列に
					auto& events = smfTrack.mapEvents.empty() ? smfTrack.mapEvents[0] : smfTrack.mapEvents.begin()->second;
					for (auto& e : metaEvents) {
						events.insert(events.lower_bound(e.position), e);	// 等価の列の先頭に挿入
					}
					smfTracks.emplace_back(std::move(smfTrack));
				}
				return smfTracks;
			}();

			// 文字列 trim 全角スペース対応
			const auto stringTrim = [](const std::string& s)->std::string {
				// boost::regex で全角スペースを正しく処理する術がわからない。バイナリとして扱うことも不可だった。
				std::smatch m;
				static const std::regex re(u8R"(^(\s|　)*([\s\S]+?)(\s|　)*$)");
				if (std::regex_search(s, m, re)) {
					if (auto sm = m[2]; sm.matched) {
						return sm.str();
					}
				}
				return "";
			};

			// sequenceName, instrumentName 毎にまとめる
			std::map<std::string, std::map<std::string, std::vector<SmfTrack>>> mapTracks;		// <sequenceName<instrumentName,evnets>>
			for (auto& smfTrack : smfTracks) {
				const auto sequenceName = [&]{
					std::string s = smfTrack.sequenceName.empty() ? "tr" : smfTrack.sequenceName;		// sequenceNameが未定義だったら "tr" とする
					return stringTrim(s);
				}();
				const auto instrumentName = stringTrim(smfTrack.instrumentName);
				auto& list = mapTracks[sequenceName][instrumentName];
				// 合成する(合成しない場合はココをお休み)
				if (!list.empty()) {
					auto &dstMapEvents = list[0].mapEvents;
					for (auto& pair : smfTrack.mapEvents) {
						const int ch = pair.first;
						auto& dst = dstMapEvents[ch];
						for (auto& e : pair.second) {
							dst.insert(e);
						}
					}
				} else {
					list.emplace_back(std::move(smfTrack));
				}
			}
			return mapTracks;
		}();

		std::string result;
		for (const auto& i : mapTracks) {
			const auto sequenceName = i.first;
			for (const auto& j : i.second) {
				const auto instrumentName = j.first;
				const auto& smfTracks = j.second;
				const auto needInstrumentName = smfTracks.size() > 1;			// 名前に InstrumentName を含める必要アリ（含めないと重複する）
				for (const auto& smfTrack : smfTracks) {
					const auto needChannelName = smfTrack.mapEvents.size() > 1;	// 名前に channel を含める必要アリ（含めないと重複する）
					for (auto pairEvent : smfTrack.mapEvents) {
						const auto channel = pairEvent.first;
						const auto trackName = [&]{
							std::string s(sequenceName);
							if (needInstrumentName) {
								s += "-" + instrumentName;
							}
							if (needChannelName) {
								s += string::format("%02d", channel);
							}

							const auto isValidName = [](const std::string& s) {
								auto r = MmlCompiler::Util::parseWord(s.begin(), s.end());
								if (r && r->word) {
									if (std::holds_alternative<std::pair<std::string::const_iterator, std::string::const_iterator>>(*r->word)) {
										return r->next == s.end();
									}
								}
								return false;
							};

							if( isValidName(s) ) return s;
							auto t = "\"" + s + "\"";				// " で括る
							if (isValidName(t)) return t;
							return u8R"(R"tr()" + s + u8R"()tr")";	// それでもダメなら R"tr(○○)tr" で括る
						}();

						const auto mml = makeMml(trackName, channel, pairEvent.second);
						result += mml;
					}
				}
			}
		}


#endif
		return result;
	}

}
