#pragma once

namespace rlib {

	template <typename T = double> class TempoListT {
	public:
		using Type = T;
		static constexpr int timeBase = 480;	// 分解能(4分音符あたりのカウント)

		struct Element {
			uint64_t	tick = 0;	// 位置
			T			tempo = 120.0;	// テンポ
			T			sec = 0.0;		// 時間(秒)

			Element()
			{}
			Element(uint64_t tick_, T tempo_)
				: tick(tick_)
				, tempo(tempo_)
			{}

			bool operator==(const Element& s)const {
				return tick == s.tick && tempo == s.tempo && sec == s.sec;
			}
			bool operator!=(const Element& s)const {
				return !(*this == s);
			}
		};

		typedef std::vector<Element> List;

		bool operator==(const TempoListT& s)const {
			return m_list == s.m_list;
		}
		bool operator!=(const TempoListT& s)const {
			return !(*this == s);
		}

		TempoListT() {
		}
		TempoListT(const TempoListT& s)
			:m_list(s.m_list)
		{}
		TempoListT(TempoListT&& s)
			:m_list(std::move(s.m_list))
		{}

		void clear() {
			m_list.clear();
		}

		// Tempo 挿入
		void insert(uint64_t position, T tempo) {
			const auto i = m_list.insert(											// m_listPos に追加
				std::upper_bound(m_list.begin(), m_list.end(), position, lessTick),	// 対象の値を超える最初
				Element(position, tempo));
			updateTime(i);	// time 更新
		}

		// 削除
		void erase(const typename List::const_iterator& i) {
			const size_t index = i - m_list.begin();
			m_list.erase(i);
			updateTime(m_list.begin() + index);	// time 更新
		}
		// 削除（指定位置の指定テンポを削除。該当するものが複数ある場合は先のもののみ削除）
		//void erase(uint64_t nStep, double tempo) {
		//	const auto r = getEqualRange(nStep);
		//	for (CList::const_iterator i = r.first; i != r.second; i++) {
		//		if (i->m_tempo == tempo) {
		//			Erase(i);
		//			break;
		//		}
		//	}
		//}

		// 時間(秒)から位置(tick)とテンポを取得(端数は切り捨て)
		auto getPositionAndTempo(T sec)const {
			sec += static_cast<T>(1e-9);	// 浮動小数点誤差で 1tick ずれないよう微小値を加える
			const auto i = std::upper_bound(m_list.begin(), m_list.end(), sec, lessSec);
			Element tempoDefault;
			const Element& t = i == m_list.begin() ? tempoDefault : *(i - 1);
			struct Result {
				uint64_t tick;
				T tempo;
			};
			return Result{ t.tick + static_cast<uint64_t>((sec - t.sec) * getCountPerSec(t.tempo)) ,t.tempo };
		}

		// 位置(tick)から時間(秒)を取得
		T getTime(uint64_t tick)const {
			const auto i = getUpperBound(tick);
			Element tempoDefault;
			const Element& t = i == m_list.begin() ? tempoDefault : *(i - 1);
			return t.sec + ((tick - t.tick) * getSecPerCount(t.tempo));
		}

		std::pair<typename List::const_iterator, typename List::const_iterator> getEqualRange(uint64_t tick)const {
			return std::make_pair(getLowerBound(tick), getUpperBound(tick));
		}

		const List& list()const {
			return m_list;
		}

		// 1秒あたりのカウント取得
		static T getCountPerSec(T tempo) {
			return tempo * timeBase / 60.0;
		}
		// 1カウントあたりの時間(秒)取得
		static T getSecPerCount(T tempo) {
			return static_cast<T>(60.0) / (tempo * static_cast<T>(timeBase));
		}
	private:
		static bool lessTick(decltype(Element::tick) a, const Element& b) { return a < b.tick; }
		static bool lessSec(decltype(Element::sec) a, const Element& b) { return a < b.sec; }

		// 位置取得 (lower_bound:対象の値以上の最初を指す)
		typename List::const_iterator getLowerBound(uint64_t tick)const {
			return std::lower_bound(m_list.begin(), m_list.end(), tick, lessTick);
		}

		// 位置取得 (upper_bound:対象の値を超える最初を指す)
		typename List::const_iterator getUpperBound(uint64_t tick)const {
			return std::upper_bound(m_list.begin(), m_list.end(), tick, lessTick);
		}

		void updateTime(typename List::iterator i) {	// time 更新
			Element tempoDefault;
			Element* pBefore = i == m_list.begin() ? &tempoDefault : &*(i - 1);
			for (; i != m_list.end(); i++) {
				i->sec = pBefore->sec + (i->tick - pBefore->tick) * getSecPerCount(pBefore->tempo);
				pBefore = &*i;
			}
		}
	private:
		List	m_list;
	};

	using TempoListF = TempoListT<float>;
	using TempoList = TempoListT<double>;

}
