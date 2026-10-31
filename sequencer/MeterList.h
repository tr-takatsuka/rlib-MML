#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <iterator>

#include "Smf.h"

namespace rlib::midi {

	struct MeterList {
		static constexpr int timeBase = Smf::defaultTimeBase;	// 分解能(4分音符あたりのカウント)

		struct Location	{	// 位置(小節)
			uint16_t	measure = 0;		// 小節( 0 ～ )
			uint8_t		beat = 0;			// 拍 ( 0 <= x < numerator )
			uint16_t	tick = 0;			// tick( 0 <= x < ticksPerBeat )
		};

		struct Meter {	// default 4/4拍子 
			uint8_t		numerator = 4;				// 分子(拍子が何個か。3/4拍子なら四分音符が3つで"3")
			uint16_t	ticksPerBeat = timeBase;	// 1拍のtick数(3/4拍子なら四分音符で"480")

			Location getLocation(uint64_t tick)const {
				Location loc;
				loc.tick = tick % ticksPerBeat;
				tick /= ticksPerBeat;
				loc.beat = tick % numerator;
				tick /= numerator;
				loc.measure = static_cast<decltype(loc.measure)>(tick);
				return loc;
			}

			uint64_t GetTick(const Location& loc)const {
				uint64_t n = loc.tick;
				n += static_cast<decltype(n)>(loc.beat) * ticksPerBeat;
				n += static_cast<decltype(n)>(loc.measure) * numerator * ticksPerBeat;
				return n;
			}

		};

		struct MeterElement
		{
			Meter		meter;
			uint16_t	measure = 0;	// 小節(0～)
			uint64_t	tick = 0;		// 位置(tick)
		};

		// tick 昇順(measure も同順で単調増加)。同一 tick の要素は挿入順。
		typedef std::vector<MeterElement> List;
		List m_list;

		// Meter 挿入
		auto insert(uint64_t nTick, const Meter& meter) {
			const auto pos = std::upper_bound(m_list.begin(), m_list.end(), nTick, lessTick);
			const auto i = m_list.insert(pos, MeterElement{ meter, 0, nTick });
			updateMeasure(i);
			return i;
		}

		// Meter 削除
		void erase(typename List::const_iterator i) {
			const auto next = m_list.erase(i);	// 以降のイテレータは無効になるので erase の戻り値を使う
			if (next != m_list.end()) updateMeasure(next);
		}

		// 位置(tick)から Meter を取得
		const Meter& getMeter(uint64_t tick)const {
			const auto i = std::upper_bound(m_list.begin(), m_list.end(), tick, lessTick);
			if (i == m_list.begin()) {
				static const Meter meter;
				return meter;
			}
			const auto& t = *std::prev(i);
			return t.meter;
		}

		// 位置(tick)が属する拍子の区間の開始位置(tick)を取得。Meter がまだ現れていなければ 0
		uint64_t getMeterTick(uint64_t tick)const {
			const auto i = std::upper_bound(m_list.begin(), m_list.end(), tick, lessTick);
			return i == m_list.begin() ? 0 : std::prev(i)->tick;
		}

		// 位置(tick)から Location を取得
		Location getLocation(uint64_t tick)const {
			const auto i = std::upper_bound(m_list.begin(), m_list.end(), tick, lessTick);
			if (i == m_list.begin()) return Meter().getLocation(tick);
			const auto& t = *std::prev(i);
			auto loc = t.meter.getLocation(tick - t.tick);
			loc.measure += t.measure;
			return loc;
		}

		// 位置(小節)から位置(tick)を取得
		uint64_t getTick(const Location& loc)const {
			const auto i = std::upper_bound(m_list.begin(), m_list.end(), loc.measure, lessMeasure);
			if (i == m_list.begin()) return Meter().GetTick(loc);
			const auto& t = *std::prev(i);
			Location l(loc);
			l.measure -= t.measure;
			return t.tick + t.meter.GetTick(l);
		}

		// SMF から MeterList を作る
		// MML の Meter で表現できないもの(分子が1未満 / 分母が128超)は対象外
		static auto fromSmf(const midi::Smf& smf) {
			struct {
				MeterList meterList;
				uint64_t endTick = 0;	// 全トラックで最後のイベントの位置を返す
			}result;
			for (const auto& track : smf.tracks) {
				if (!track.events.empty()) result.endTick = std::max<decltype(result.endTick)>(result.endTick, track.events.rbegin()->first);
				for (const auto& event : track.events) {
					const auto e = std::dynamic_pointer_cast<const midi::EventMeta>(event.second);
					if (!e || e->type != midi::EventMeta::Type::meter || e->data.size() != 4) continue;
					const int nn = e->data[0], dd = e->data[1];
					if (nn < 1 || dd > 7) continue;
					result.meterList.insert(event.first, MeterList::Meter{ static_cast<uint8_t>(nn), static_cast<uint16_t>(smf.timeBase * 4 >> dd) });
				}
			}
			return result;
		}

	private:
		static bool lessTick(uint64_t tick, const MeterElement& e) { return tick < e.tick; }
		static bool lessMeasure(uint16_t measure, const MeterElement& e) { return measure < e.measure; }

		void updateMeasure(typename List::iterator i) {	// i 以降の measure 更新
			for (; i != m_list.end(); ++i) {
				const MeterElement defaultElement;	// 先頭の前には 4/4拍子 (tick=0, measure=0) があるものとする
				const MeterElement& prev = i == m_list.begin() ? defaultElement : *std::prev(i);
				// 直前の拍子で、直前の要素から i までの距離を小節換算する
				const Location loc = prev.meter.getLocation(i->tick - prev.tick);
				i->measure = static_cast<uint16_t>(prev.measure + loc.measure);
				if (loc.beat > 0 || loc.tick > 0) {	// 小節の途中なら、ここから新しい小節とする
					i->measure += 1;
				}
			}
		}
	};

}
