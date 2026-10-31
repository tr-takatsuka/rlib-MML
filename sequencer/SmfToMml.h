#pragma once

// SMF -> MML
//   smfToMml    : SMF -> Tracks (smfToTracks) -> MML (tracksToMml)
//   smfToTracks : SMF -> std::vector<MmlCompiler::Track>。MmlToSmf.h (Tracks -> SMF) の逆変換。
//     ・Track は (Port, ch) ごと。SMFの1トラックに複数の (Port, ch) があれば複数の Track になる
//     ・NoteOn と NoteOff は1つの MmlCompiler::EventNote(音長付き)にまとめる
//     ・EndOfTrack は EventMeta(type 0x2f)として残す
//     ・Track に表現できない情報(ノートオフのベロシティ、プレッシャーなど)は捨てる
//   tracksToMml : std::vector<MmlCompiler::Track> -> MML

#include <boost/regex.hpp>
#ifndef __EMSCRIPTEN__
#include <boost/locale/encoding.hpp> 
#endif
#include <format>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <compare>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <typeindex>
#include <vector>

#include "MidiEvent.h"
#include "Smf.h"
#include "MeterList.h"
#include "MmlCompiler.h"

#include "../stringformat/StringFormat.h"
#include "../json/Json.h"

namespace rlib::sequencer {

	namespace detail {

		// 文字列 trim 全角スペース対応
		// boost::regex を使う。理由: (1) std::regex(ECMAScript)は \a や \e を「文字 a / e」と解釈し、末尾の a / e まで削ってしまう
		//                             (2) MSVC と Emscripten では std::regex_search が落ちるケースがある
		inline std::string stringTrim(const std::string& s) {
			static const boost::regex re(R"(^([ \a\b\e\f\n\r\t\v]|　)+|([ \a\b\e\f\n\r\t\v]|　)+$)");	// "\s"は日本語でヘンになる
			return boost::regex_replace(s, re, "");
		}

		// バイト列を文字列(UTF-8)にする。文字列として扱えなければ std::nullopt
		inline std::optional<std::string> decodeText(const std::string& bin) {
			try {
				const auto hasControlCode = [](const std::string& s) {
					// std::regex は MSVC と Emscripten で regex_search が落ちるケースがあるので boost::regex を使う
					static const boost::regex controlCode(R"([\x00-\x08\x0B-\x1F\x7F])");	// 改行(\x0A)とタブ(\x09)は許可し、他の制御文字を検出
					return boost::regex_search(s, controlCode);
				};
#ifndef __EMSCRIPTEN__
				const auto sjisToUtf8 = [](const std::string& sjisStr)->std::optional<std::string> {
					try {
						return boost::locale::conv::to_utf<char>(sjisStr.c_str(), "Shift-JIS");
					} catch (...) {
					}
					return std::nullopt;
				};

				// SJIS
				if (const auto s = sjisToUtf8(bin)) {
					if (!hasControlCode(*s)) {
						return s;
					}
				}
#endif

				// そのまま
				if (!hasControlCode(bin)) {
					return bin;
				}
			} catch (...) {
			}
			return std::nullopt;
		}

	}

	inline std::vector<MmlCompiler::Track> smfToTracks(const midi::Smf& smf) {
		using Smf = midi::Smf;

		const auto smf2 = Smf::convertTimebase(smf, MmlCompiler::timeBase);		// timebaseを480に

		// MMLのTrackに対応する単位。SMFの1トラックを (Port, ch) で分ける(Portが途中で切り替わっていれば別々のTrackになる)
		struct TrackKey {
			int port = -1;		// MIDI Port(0～255)。-1: Portメタが無い(指定なし)
			int channel = 0;	// ch (0～15)
			auto operator<=>(const TrackKey&) const = default;
		};
		using MapEvents = std::map<TrackKey, Smf::Events>;	// <(port,ch),evnets>

		struct SmfTrack {
			std::string	sequenceName = "unknown";
			std::string	instrumentName = "unknown";
			MapEvents	mapEvents;		// <(port,ch),evnets>
		};
		std::vector<SmfTrack> smfTracks = [&](const midi::Smf& smf) {
			std::vector<SmfTrack> tracks;
			for (auto& track : smf.tracks) {
				SmfTrack smfTrack;
				struct {
					bool sequenceName = false;
					bool instrumentName = false;
				}exists;
				int currentPort = -1;						// 現在のPort(Portメタで更新。最初のPortメタより前は -1)
				std::optional<int> firstPort;				// 最初に現れたPort
				std::vector<std::pair<int, Smf::Event>> others;	// チャンネルを持たないイベントと、その時点のPort
				for (auto& event : track.events) {
					if (auto e = std::dynamic_pointer_cast<const midi::EventCh>(event.second)) {
						smfTrack.mapEvents[TrackKey{ currentPort, e->channel }].insert(event);
					} else if (auto e = std::dynamic_pointer_cast<const midi::EventMeta>(event.second)) {

						if (e->type == midi::EventMeta::Type::port && e->data.size() == 1) {	// MIDI Port メタ
							currentPort = e->data[0];				// 以降のイベントはこのPortへ。Track の port で表すので、イベントとしては出力しない
							if (!firstPort) firstPort = currentPort;
							continue;
						}

						const auto name = [&](const std::string& s)->std::optional<std::string> {
							const auto decoded = detail::decodeText(s).value_or(s);
							if (auto result = detail::stringTrim(decoded); !result.empty()) return result;
							return std::nullopt;
						};

						switch (e->type) {
						case midi::EventMeta::Type::sequenceName:
							if (!exists.sequenceName) {
								exists.sequenceName = true;
								smfTrack.sequenceName = name(e->getText()).value_or(smfTrack.sequenceName);
							}
							break;
						case midi::EventMeta::Type::instrumentName:
							if (!exists.instrumentName) {
								exists.instrumentName = true;
								smfTrack.instrumentName = name(e->getText()).value_or(smfTrack.instrumentName);
							}
							break;
						default:
							break;
						}
						others.emplace_back(currentPort, event);
					} else if (auto e = std::dynamic_pointer_cast<const midi::EventSystemExclusive>(event.second)) {
						others.emplace_back(currentPort, event);
					} else {
						assert(false);
					}
				}

				// チャンネルを持たないイベントは、その時点のPortを持つ一番若いchのTrackに合成。そのPortのTrackが無ければ、全体で一番若いTrackに合成。
				// チャンネルのイベントが1つもなければ ch0 のTrack(Portは最初に現れたもの)を作って合成。
				if (!others.empty()) {
					if (smfTrack.mapEvents.empty()) smfTrack.mapEvents[TrackKey{ firstPort.value_or(-1), 0 }];
					for (auto it = others.rbegin(); it != others.rend(); it++) {
						const auto g = smfTrack.mapEvents.lower_bound(TrackKey{ it->first, 0 });
						const auto target = (g != smfTrack.mapEvents.end() && g->first.port == it->first) ? g->first : smfTrack.mapEvents.begin()->first;
						auto& map = smfTrack.mapEvents[target];
						auto i = map.lower_bound(it->second.first);
						map.insert(i, it->second);
					}
				}

				tracks.emplace_back(std::move(smfTrack));
			}
			return tracks;
		}(smf2);

		{// 同名のSequenceNameには番号（ (1),(2)・・・ ）を付与する
			std::set<std::string> used;
			std::vector<decltype(smfTracks)::iterator> itList;
			for (auto it = smfTracks.begin(); it != smfTracks.end(); it++) {
				if (auto i = used.find(it->sequenceName); i == used.end()) {
					used.insert(it->sequenceName);
				} else {
					itList.push_back(it);	// 重複したらキューに
				}
			}
			for (auto it : itList) {
				for (int n = 1; true; n++) {
					auto candidate = string::format("%s(%d)", it->sequenceName, n);
					if (auto i = used.find(candidate); i == used.end()) {
						it->sequenceName = candidate;
						used.insert(candidate);
						break;
					}
				}
			}
		}

		// 1つの(Port, ch)のSMFイベント -> Trackのイベント
		const auto toEventList = [](const Smf::Events& events) {
			MmlCompiler::EventList result;
			const std::vector<Smf::Event> v(events.begin(), events.end());

			// ノートオフか？(NoteOff か、velocity 0 の NoteOn)
			const auto asNote = [](const Smf::Event& e)->std::optional<std::pair<const midi::EventNote*, bool>> {	// <note, isOff>
				if (const auto n = std::dynamic_pointer_cast<const midi::EventNote>(e.second)) {
					return std::pair{ n.get(), n->velocity <= 0 || std::dynamic_pointer_cast<const midi::EventNoteOff>(e.second) != nullptr };
				}
				return std::nullopt;
			};

			// note ごとの NoteOff / NoteOn(velocity > 0) の出現位置(v のインデックス)
			std::array<std::vector<size_t>, 128> offs, ons;
			for (size_t i = 0; i < v.size(); i++) {
				if (const auto n = asNote(v[i])) {
					(n->second ? offs : ons)[n->first->note & 0x7f].push_back(i);
				}
			}

			// 音の長さ
			//   同じ note の最初の NoteOff まで。
			//   NoteOff がなければ、次の同じ note の NoteOn まで。ただし8分音符を上限とする(SmfCompare.h も同じ規則)
			const auto getLength = [&](size_t i, int note)->size_t {
				const auto tick = v[i].first;
				const auto& off = offs[note & 0x7f];
				if (const auto j = std::upper_bound(off.begin(), off.end(), i); j != off.end()) {
					return v[*j].first - tick;
				}
				size_t len = MmlCompiler::timeBase / 2;
				const auto& on = ons[note & 0x7f];
				for (auto j = std::upper_bound(on.begin(), on.end(), i); j != on.end(); ++j) {
					if (v[*j].first <= tick) continue;				// 同じ位置は対象外
					if (v[*j].first - tick < len) len = v[*j].first - tick;
					break;
				}
				return len;
			};

			for (size_t i = 0; i < v.size(); i++) {
				const auto tick = v[i].first;
				const auto& ev = *v[i].second;
				if (const auto n = asNote(v[i])) {
					if (n->second) continue;						// NoteOff は NoteOn 側の音長になる
					auto e = std::make_shared<MmlCompiler::EventNote>();
					e->note = n->first->note;
					e->velocity = n->first->velocity;
					e->length = getLength(i, n->first->note);
					result.emplace(tick, std::move(e));
				} else if (const auto e = dynamic_cast<const midi::EventControlChange*>(&ev)) {
					auto r = std::make_shared<MmlCompiler::EventControlChange>();
					r->no = static_cast<uint8_t>(e->type);
					r->value = e->value;
					result.emplace(tick, std::move(r));
				} else if (const auto e = dynamic_cast<const midi::EventProgramChange*>(&ev)) {
					auto r = std::make_shared<MmlCompiler::EventProgramChange>();
					r->programNo = e->programNo;
					result.emplace(tick, std::move(r));
				} else if (const auto e = dynamic_cast<const midi::EventPitchBend*>(&ev)) {
					auto r = std::make_shared<MmlCompiler::EventPitchBend>();
					r->pitchBend = e->pitchBend;
					result.emplace(tick, std::move(r));
				} else if (const auto e = dynamic_cast<const midi::EventSystemExclusive*>(&ev)) {
					auto r = std::make_shared<MmlCompiler::EventSystemExclusive>();
					r->data = e->data;
					result.emplace(tick, std::move(r));
				} else if (const auto e = dynamic_cast<const midi::EventMeta*>(&ev)) {
					switch (e->type) {
					case midi::EventMeta::Type::port:				// Track の port
					case midi::EventMeta::Type::sequenceName:		// Track の name
					case midi::EventMeta::Type::instrumentName:		// Track の instrument
						continue;
					default:
						break;
					}
					auto r = std::make_shared<MmlCompiler::EventMeta>();
					r->type = static_cast<uint8_t>(e->type);
					r->data = e->data;
					result.emplace(tick, std::move(r));
				}
				// 上記以外(ポリフォニックキープレッシャー / チャンネルプレッシャー)は対象外
			}
			return result;
		};

		std::vector<MmlCompiler::Track> result;
		for (auto& smfTrack : smfTracks) {
			const bool singlePort = std::all_of(smfTrack.mapEvents.begin(), smfTrack.mapEvents.end(), [&](const auto& g) { return g.first.port == smfTrack.mapEvents.begin()->first.port; });
			for (auto it = smfTrack.mapEvents.begin(); it != smfTrack.mapEvents.end(); it++) {
				const int channel = it->first.channel;
				const int port = it->first.port;
				MmlCompiler::Track track;
				track.name = smfTrack.sequenceName;
				if (it != smfTrack.mapEvents.begin()) {		// 複数Trackになる場合は ch(と、Portが複数あるときは Port)を名前に含める
					track.name += (singlePort || port < 0) ? string::format("-%02d", channel + 1) : string::format("-p%d-%02d", port, channel + 1);
				}
				track.instrument = smfTrack.instrumentName;
				track.channel = static_cast<uint8_t>(channel);
				if (port >= 0) track.port = static_cast<uint8_t>(port);
				track.eventList = toEventList(it->second);
				result.push_back(std::move(track));
			}
		}
		return result;
	}

	namespace detail {

		// Track の拍子(Meter メタ)から MeterList を作る。endTick は全Trackで最後のイベント(音符は終端)の位置
		// MML の Meter で表現できないもの(分子が1未満 / 分母が128超)は対象外
		inline auto meterListFromTracks(const std::vector<MmlCompiler::Track>& tracks) {
			struct {
				midi::MeterList	meterList;
				size_t			endTick = 0;
			}result;
			for (const auto& track : tracks) {
				for (const auto& event : track.eventList) {
					size_t end = event.first;
					if (const auto n = std::dynamic_pointer_cast<const MmlCompiler::EventNote>(event.second)) {
						end += n->length;
					} else if (const auto e = std::dynamic_pointer_cast<const MmlCompiler::EventMeta>(event.second)) {
						if (e->type != static_cast<uint8_t>(midi::EventMeta::Type::meter) || e->data.size() != 4) continue;
						const int nn = e->data[0], dd = e->data[1];
						if (nn < 1 || dd > 7) continue;
						result.meterList.insert(event.first, midi::MeterList::Meter{ static_cast<uint8_t>(nn), static_cast<uint16_t>(MmlCompiler::timeBase * 4 >> dd) });
					}
					result.endTick = (std::max)(result.endTick, end);
				}
			}
			return result;
		}

	}

	inline std::string tracksToMml(const std::vector<MmlCompiler::Track>& tracks) {

		// 文字列を必要に応じて " " や R"( )" で括る 
		const auto safeText = [](const std::string& text) {
			const auto isValidName = [](const std::string& s) {
				auto r = MmlCompiler::Util::parseWord(s);
				if (r && r->word) {
					if (std::holds_alternative<std::string_view>(*r->word)) {
						return r->next.empty();
					}
				}
				return false;
			};
			if (isValidName(text)) return text;
			if (auto t = string::format(R"("%s")", text); isValidName(t)) {	// ダメなら " " で括る
				return t;
			}
			if (auto t = string::format(R"_(R"(%s)")_", text); isValidName(t)) {	// ダメなら R"( )" で括る
				return t;
			}
			for (int i = 0; i < 1000; i++) {
				if (auto t = string::format(R"(R"t%d(%s)t%d")", i, text, i); isValidName(t)) {	// それでもダメなら R"t?(○○)t?" で括る
					return t;
				}
			}
			return string::format(R"(R"t_(%s)t_")", text);	// 最後は R"t_(○○)t_" で諦める
		};

		// 1トラック分のMML
		struct MmlTrack {
			std::string					header;		// CreateTrack(...)
			std::vector<std::string>	blocks;		// blocks[n] は n番目のブロックのMML
		};

		// MeterList と blockStarts(ブロックを区切る位置配列)
		const auto [meterList, blockStarts, endTick] = [&] {
			using namespace midi;
			const auto [meterList, endTick] = detail::meterListFromTracks(tracks);

			// ブロックの開始位置を求める。先頭から4小節ごと。曲の途中でMeterが現れたら、そこから新しいブロックを開始する
			std::vector<size_t> blockStarts;
			{
				struct Section {
					size_t				tick;
					MeterList::Meter	meter;
				};
				std::vector<Section> sections;
				sections.push_back({ 0, MeterList::Meter() });	// 先頭は 4/4 拍子
				for (const auto& e : meterList.m_list) {
					if (e.tick == sections.back().tick) {
						sections.back().meter = e.meter;		// 同じ位置なら後勝ち
					} else {
						sections.push_back({ static_cast<size_t>(e.tick), e.meter });
					}
				}
				constexpr size_t measuresPerBlock = 4;
				for (size_t i = 0; i < sections.size(); i++) {
					const size_t to = i + 1 < sections.size() ? sections[i + 1].tick : std::numeric_limits<size_t>::max();
					const size_t blockLen = measuresPerBlock * sections[i].meter.numerator * sections[i].meter.ticksPerBeat;
					for (size_t tick = sections[i].tick; tick < to && tick <= endTick; tick += blockLen) {
						blockStarts.push_back(tick);
					}
				}
			}

			return std::tuple{ meterList,blockStarts,endTick };
		}();

		using EventIt = MmlCompiler::EventList::const_iterator;

		const auto makeMml = [&](const MmlCompiler::Track& track) {

			const auto& events = track.eventList;
			const int channel = track.channel;
			const int port = track.port ? *track.port : -1;		// port: -1 は指定なし

			struct {
				int		note = -1;
				size_t	position = 0;	// 現在の位置
				int		velocity = -1;
				bool	noteUnmove = false;				// 音符で位置を更新しないモード
			}state;

			std::vector<std::string> mmls(1);

			// 音長 文字列取得
			const auto getLengthText = [&](size_t len, size_t position, bool movePosition)->std::vector<std::string> {
				if (len <= 0) return { "!0" };
				std::vector<std::string> result(1);
				auto current = position;
				for (int i = 0; len > 0; i++) {

					// pos より後ろで最初のブロック開始位置(なければ size_t の最大値)
					const auto nextBlockStart = [&](size_t pos)->size_t {
						const auto it = std::upper_bound(blockStarts.begin(), blockStarts.end(), pos);
						return it == blockStarts.end() ? std::numeric_limits<size_t>::max() : *it;
					};

					const auto lenTo = [&](size_t targetLen) {

						const auto add = [&](const std::string& code, size_t codeLen = 0) {
							if (i > 0) *result.rbegin() += "^";
							*result.rbegin() += code;

							auto before = current;
							current += codeLen ? codeLen : targetLen;
							len -= codeLen ? codeLen : targetLen;

							if (movePosition && current >= nextBlockStart(before)) {	// ブロックの区切りに達したら次のブロックへ
								result.push_back("");
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
						case 120:					add("");		return;
						case dotLen(120, 1):		add(".");		return;
						case dotLen(120, 2):		add("..");		return;
						case 480 / 3:				add("12");		return;
						case dotLen(480 / 3, 2):	add("12..");	return;
						case 240:					add("^");		return;
						case dotLen(240, 1):		add("^^");		return;
						case dotLen(240, 2):		add("8..");		return;
						case dotLen(240, 3):		add("8...");	return;
						case 480:					add("4");		return;
						case dotLen(480, 1):		add("4.");		return;
						case dotLen(480, 2):		add("4..");		return;
						case dotLen(480, 3):		add("4...");	return;
						case 960:					add("2");		return;
						case dotLen(960, 1):			add("2.");		return;
						case dotLen(960, 2):			add("2..");		return;
						case dotLen(960, 3):			add("2...");	return;
							// case 960 + 240:			add("2^");		return;
						case 1920:					add("1");		return;
						default:	break;
						}
						if (targetLen > 1920) {
							add("1", 1920);
							return;
						}
						add(string::format("!%d", targetLen));
					};

					// ブロックの区切りをまたがないように、次の区切りまでで音長を制限する
					const size_t limit = movePosition ? std::min<size_t>(len, nextBlockStart(current) - current) : len;

					// 小節の区切り位置は、拍子の区間の開始位置から数える
					const auto& meter = meterList.getMeter(current);
					const size_t measureLen = meter.ticksPerBeat * meter.numerator;	// 1小節の長さ
					const size_t measureStart = meterList.getMeterTick(current);	// 現在の拍子の区間の開始位置

					if (auto mod = (current - measureStart) % measureLen) {	// 区切り位置の余り
						auto n = measureLen - mod;
						if (limit >= n) {
							lenTo(n);
							continue;
						}
					}

					lenTo(limit);
				}
				return result;
			};

			const std::map<std::type_index, std::function<EventIt(const EventIt&)> > map = {
				{ typeid(MmlCompiler::EventNote), [&](const EventIt& it) {
					auto& e = static_cast<const MmlCompiler::EventNote&>(*it->second);
					const size_t len = e.length;

					std::string r;

					// velocity
					if (state.velocity != e.velocity) {
						state.velocity = e.velocity;
						r += string::format("v%d", state.velocity);
					}

					static const std::vector<std::string> noteTable = {
						"c","c+","d", "d+", "e", "f", "f+", "g", "g+", "a", "a+", "b"
					};
					const auto& note = noteTable[e.note % noteTable.size()];
					const int oct = e.note / static_cast<int>(noteTable.size());

					if (state.note < 0) {
						r += string::format(" o%d ", oct - 2);
						state.note = e.note;
					}

					const int diffOct = oct - (state.note / static_cast<int>(noteTable.size()));
					for (int i = 0; i < std::abs(diffOct); i++) {
						r += diffOct > 0 ? "<" : ">";
					}
					r += string::format("%s", note);
					state.note = e.note;

					// 音の終わりより先にイベントがあるか？(重なるか？)
					const bool overlap = [&] {
						auto i = it;
						i++;
						return i != events.end() && i->first < it->first + len;
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
					return std::next(it);
				} },

				{ typeid(MmlCompiler::EventControlChange), [&](const EventIt& it) {
					auto& e = static_cast<const MmlCompiler::EventControlChange&>(*it->second);
					switch (static_cast<midi::EventControlChange::Type>(e.no)) {
					case midi::EventControlChange::Type::volume:
						*mmls.rbegin() += string::format("V(%s)", static_cast<int>(e.value));
						return std::next(it);
					case midi::EventControlChange::Type::expression:
						*mmls.rbegin() += string::format("Ep(%s)", static_cast<int>(e.value));
						return std::next(it);
					case midi::EventControlChange::Type::pan:
						*mmls.rbegin() += string::format("Pan(%s)", static_cast<int>(e.value));
						return std::next(it);
					case midi::EventControlChange::Type::rpnMSB: {
						constexpr auto N = 4;	// 4個必要
						std::vector<std::pair<size_t, std::shared_ptr<const MmlCompiler::EventControlChange>>> list;
						auto next = it;
						for (; next != std::ranges::next(it, N, events.end()); next++) {
							auto ev = std::dynamic_pointer_cast<const MmlCompiler::EventControlChange>(next->second);
							if (!ev) break;
							list.emplace_back(next->first, ev);
						}
						if (list.size() < N) break;
						if (static_cast<midi::EventControlChange::Type>(list[1].second->no) != midi::EventControlChange::Type::rpnLSB) break;
						if (static_cast<midi::EventControlChange::Type>(list[2].second->no) != midi::EventControlChange::Type::dataEntryMSB) break;
						if (static_cast<midi::EventControlChange::Type>(list[3].second->no) != midi::EventControlChange::Type::dataEntryLSB) break;
						midi::utility::Bit14 type;
						type.msb = list[0].second->value;
						type.lsb = list[1].second->value;
						midi::utility::Bit14 value;
						value.msb = list[2].second->value;
						value.lsb = list[3].second->value;

						// FineTune
						if (type.value == static_cast<uint16_t>(midi::EventControlChange::RpnType::fineTune)) {
							const auto toFineTune = [](uint16_t raw) {
								constexpr double inMin = -100.0, inMax = 100.0;
								constexpr int outMax = 16383;
								return static_cast<double>(raw) * (inMax - inMin) / (outMax + 1) + inMin;	// -8192～0～8192スケール(+100には微妙に届かない)
							};
							double fineTune = toFineTune(value.value);
							const double v = std::round(fineTune * 100.0) / 100.0;	// 小数点第2位で丸める
							*mmls.rbegin() += string::format(std::string("FineTune") + (std::floor(v) == v ? "(%.0f)" : "(%.2f)"), v);
							return next;
						}

						// CoarseTune
						if (type.value == static_cast<uint16_t>(midi::EventControlChange::RpnType::coarseTune)) {
							const int coarseTune = value.value / 0x80 - 64;
							*mmls.rbegin() += string::format("CoarseTune(%d)", coarseTune);
							return next;
						}

						break;
					}
					default:
						break;
					}
					// バイナリデータとして出力
					*mmls.rbegin() += string::format("CC(%s,%s)", static_cast<int>(e.no), static_cast<int>(e.value));
					return std::next(it);
				} },

				{ typeid(MmlCompiler::EventProgramChange), [&](const EventIt& it) {
					auto& e = static_cast<const MmlCompiler::EventProgramChange&>(*it->second);
					*mmls.rbegin() += string::format("@%d", static_cast<int>(e.programNo));
					return std::next(it);
				} },

				{ typeid(MmlCompiler::EventPitchBend), [&](const EventIt& it) {
					auto& e = static_cast<const MmlCompiler::EventPitchBend&>(*it->second);
					*mmls.rbegin() += string::format("PitchBend(%d)", static_cast<int>(e.pitchBend));
					return std::next(it);
				} },

				{ typeid(MmlCompiler::EventSystemExclusive), [&](const EventIt& it) {
					auto& e = static_cast<const MmlCompiler::EventSystemExclusive&>(*it->second);

					auto isMatch = [](const std::vector<uint8_t>& data, const std::initializer_list<int16_t>& pattern) {
						if (data.size() < pattern.size()) return false;
						auto it = data.begin();
						for (auto p : pattern) {
							if (p >= 0 && *it != p) return false;	// 0未満はワイルドカード
							it++;
						}
						return true;
					};

					// マスターボリューム(deviceId=0x7f のみ)
					if (isMatch(e.data,{ 0xf0, 0x7f, 0x7f, 0x4, 0x1, -1, -1 })) {
						midi::utility::Bit14 u;
						u.lsb = e.data[5];
						u.msb = e.data[6];
						*mmls.rbegin() += string::format("\nMasterVolume(%d)", u.value);
						return std::next(it);
					}

					// その他
					const auto join = [](const std::vector<uint8_t>& v, const std::string& sep)->std::string {
						std::ostringstream oss;
						for (size_t i = 0; i < v.size(); i++) {
							if (i > 0) oss << sep;
							oss << std::format("0x{:02x}", v[i]);
						}
						return oss.str();
					};

					*mmls.rbegin() += string::format("\nSysEx(%s)", join(e.data, ","));
					return std::next(it);
				} },

				{ typeid(MmlCompiler::EventMeta), [&](const EventIt& it) {
					const auto& src = static_cast<const MmlCompiler::EventMeta&>(*it->second);
					const midi::EventMeta e(static_cast<midi::EventMeta::Type>(src.type), std::vector<uint8_t>(src.data));	// テキストやテンポの取得用
					switch (e.type) {
					case midi::EventMeta::Type::tempo:
						*mmls.rbegin() += string::format("t%s", e.getTempo());
						return std::next(it);
					case midi::EventMeta::Type::meter:		// 拍子 Meter(分子,分母,clock:cc,n32:bb) ※ cc=24, bb=8 (デフォルト値)なら省略
						if (e.data.size() == 4) {
							const int nn = e.data[0], dd = e.data[1], cc = e.data[2], bb = e.data[3];
							if (nn >= 1 && dd <= 7) {		// MML で表現できる範囲(分子 1～255 / 分母 1～128)。範囲外は下の Meta(type:0x58,…) で出力
								std::string s = string::format("\nMeter(%d,%d", nn, 1 << dd);
								if (cc != 24) s += string::format(",clock:%d", cc);
								if (bb != 8) s += string::format(",n32:%d", bb);
								*mmls.rbegin() += s + ")";
								return std::next(it);
							}
						}
						break;
					case midi::EventMeta::Type::sequencerLocal:
						try {
							// rlib-MML 固有情報なら展開する
							if (const auto json = Json::parse(e.getText())["rlib-MML"]; !json.isNull()) {

								// FM音色定義(DefinePresetFM)
								if (const auto& map = json["opnfm"].get<Json::Map>(); !map.empty()) {
									for (const auto it : map) {
										const auto no = std::stoi(it.first);
										const auto& name = it.second["name"].get<std::string>();
										const auto& data = it.second["reg"].get<Json::Array>();
										*mmls.rbegin() += string::format(
											"\nDefinePresetFM(no:%d, name:%s,\n"
											"// AR  DR  SR  RR  SL  TL KS ML DT\n"
											"  %3d,%3d,%3d,%3d,%3d,%3d,%2d,%2d,%2d,\n"
											"  %3d,%3d,%3d,%3d,%3d,%3d,%2d,%2d,%2d,\n"
											"  %3d,%3d,%3d,%3d,%3d,%3d,%2d,%2d,%2d,\n"
											"  %3d,%3d,%3d,%3d,%3d,%3d,%2d,%2d,%2d,\n"
											"// AL  FB\n"
											"  %3d,%3d)\n"
											, no, safeText(name), data);
									}
								}

								// PSG(SSG)音色定義(DefinePresetPSG)
								if (const auto& map = json["opnpsg"].get<Json::Map>(); !map.empty()) {
									const auto formatEnvValue = [](double v)->std::string {
										auto s = string::format("%g", v);
										if (s.find('.') == std::string::npos && v >= 0.0 && v <= 9.0) s += ".0";	// 0～9の正数の場合は小数点第一位(.0)を付ける
										return s;
									};
									for (const auto it : map) {
										const auto no = std::stoi(it.first);
										const auto& name = it.second["name"].get<std::string>();
										const auto& data = it.second["reg"].get<Json::Array>();	// AR,HR,DR,SL,RR,noise,tone
										if (data.size() != 7) continue;
										const auto ar = formatEnvValue(data[0].get<double>());
										const auto hr = formatEnvValue(data[1].get<double>());
										const auto dr = formatEnvValue(data[2].get<double>());
										const auto sl = formatEnvValue(data[3].get<double>());
										const auto rr = formatEnvValue(data[4].get<double>());
										const auto noise = data[5].get<std::intmax_t>();
										const auto tone = data[6].get<std::intmax_t>();
										*mmls.rbegin() += string::format(
											"\nDefinePresetPSG(no:%d, name:%s,\n"
											"// AR   HR   DR   SL   RR   noise tone\n"
											"  %s, %s, %s, %s, %s,  %d,  %d)\n"
											, no, safeText(name), ar, hr, dr, sl, rr, noise, tone);
									}
								}

								return std::next(it);
							}
						} catch (...) {
						}
						break;
					case midi::EventMeta::Type::sequenceName:
					case midi::EventMeta::Type::instrumentName:
						return std::next(it);			// 何も出力しない
					case midi::EventMeta::Type::text:
					case midi::EventMeta::Type::copyright:
					case midi::EventMeta::Type::words:
						if (auto text = detail::decodeText(e.getText())) {		// 文字列？
							const auto s = safeText(*text);			// 必要に応じて " で括る
							*mmls.rbegin() += string::format("\nMeta(type:0x%x,%s)", static_cast<unsigned int>(e.type), s);
							return std::next(it);
						}
						break;
					default:
						break;
					}
					// バイナリデータとして出力
					const auto toJoinString = [](const std::vector<uint8_t>& v)-> std::string {	// std::vector<uint8_t> をカンマ区切りの文字列に変換
						std::ostringstream oss;
						for (auto& n : v) {
							oss << "," << static_cast<unsigned int>(n);
						}
						return oss.str();
					};
					*mmls.rbegin() += string::format("\nMeta(type:0x%x%s)", static_cast<unsigned int>(e.type), toJoinString(e.data));
					return std::next(it);
				} },

			};

			for (auto it = events.begin(); it != events.end(); ) {
				// delta time (休符)
				if (const size_t len = it->first - state.position; len > 0) {
					auto vLen = getLengthText(len, state.position, true);
					*mmls.rbegin() += string::format("r%s", vLen[0]);
					for (auto i = 1; i < vLen.size(); i++) {
						mmls.emplace_back(std::move(vLen[i]));
					}
					state.position = it->first;
				}
				const auto& ev = *it->second;
				if (auto i = map.find(typeid(ev)); i != map.end()) {
					it = (i->second)(it);
				} else {
					// assert(false);
					it++;
				}
			}

			const auto instrumentText = track.instrument.empty() ? "" : string::format("instrument:%s, ", safeText(track.instrument));
			const auto portText = port < 0 ? "" : string::format(", port:%d", port);	// Port指定がなければ省略(port:0 とは区別する)
			MmlTrack result;
			result.header = string::format("CreateTrack(name:%s, %schannel:%d%s) l16", safeText(track.name), instrumentText, channel + 1, portText);
			result.blocks = std::move(mmls);
			return result;
		};

		// 各Trackの MML を作る
		struct OutTrack {
			std::string	name;		// Track() で参照する名前
			MmlTrack	mml;
		};
		std::vector<OutTrack> outTracks;
		for (const auto& track : tracks) {
			outTracks.push_back({ safeText(track.name), makeMml(track) });
		}

		const auto isBlank = [](const std::string& s) {
			return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; });
		};

		std::string result;

		// 先頭に CreateTrack
		for (const auto& track : outTracks) {
			result += track.mml.header + "\n";
		}

		// 以降はブロックごとに、全トラックを並べる
		for (size_t b = 0; b < blockStarts.size(); b++) {
			std::string text;
			for (const auto& track : outTracks) {
				if (b >= track.mml.blocks.size() || isBlank(track.mml.blocks[b])) continue;
				const auto& mml = track.mml.blocks[b];
				text += "Track(" + track.name + ")";
				if (!std::isspace(static_cast<unsigned char>(mml.front()))) text += " ";
				text += mml + "\n";
			}
			if (text.empty()) continue;

			// 小節番号
			const size_t first = meterList.getLocation(blockStarts[b]).measure;
			const size_t endPos = b + 1 < blockStarts.size() ? blockStarts[b + 1] : std::max<size_t>(endTick, 1);
			const size_t last = std::max(first, static_cast<size_t>(meterList.getLocation(endPos - 1).measure));
			result += first == last ? string::format("\n// %d\n", first + 1) : string::format("\n// %d - %d\n", first + 1, last + 1);	// 表示上は 1～ とする
			result += text;
		}

		return result;
	}

	// SMF -> MML
	inline std::string smfToMml(const midi::Smf& smf) {
		return tracksToMml(smfToTracks(smf));
	}

}
