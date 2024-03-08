#pragma once

namespace rlib {

	template <typename T = double> class TempoListT {
	public:
		using Type = T;
		enum {
			timeBase = 480,			// ����\(4������������̃J�E���g)
		};
		struct Element {
			uint64_t	position = 0;	// �ʒu
			T			tempo = 120.0;	// �e���|
			T			time = 0.0;		// ����(�b)

			Element()
			{}
			Element(uint64_t position_, T tempo_)
				:position(position_)
				, tempo(tempo_)
			{}

			bool operator==(const Element& s)const {
				return position == s.position && tempo == s.tempo && time == s.time;
			}
			bool operator!=(const Element& s)const {
				return !(*this == s);
			}
		};
		struct LessPosition {
			bool operator()(const Element& a, const Element& b)				const { return a.position < b.position; }
			bool operator()(decltype(Element::position) a, const Element& b)const { return a < b.position; }
			bool operator()(const Element& a, decltype(Element::position) b)const { return a.position < b; }
		};
		struct LessTime {
			bool operator()(const Element& a, const Element& b)			const { return a.time < b.time; }
			bool operator()(decltype(Element::time) a, const Element& b)const { return a < b.time; }
			bool operator()(const Element& a, decltype(Element::time) b)const { return a.time < b; }
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

		// Tempo �}��
		void insert(uint64_t position, T tempo) {
			const auto i = m_list.insert(												// m_listPos �ɒǉ�
				std::upper_bound(m_list.begin(), m_list.end(), position, LessPosition()),	// �Ώۂ̒l�𒴂���ŏ�
				Element(position, tempo));
			updateTime(i);	// time �X�V
		}

		// �폜
		void erase(const typename List::const_iterator& i) {
			const size_t index = i - m_list.begin();
			m_list.erase(i);
			updateTime(m_list.begin() + index);	// time �X�V
		}
		// �폜�i�w��ʒu�̎w��e���|���폜�B�Y��������̂���������ꍇ�͐�̂��̂̂ݍ폜�j
		//void erase(uint64_t nStep, double tempo) {
		//	const auto r = getEqualRange(nStep);
		//	for (CList::const_iterator i = r.first; i != r.second; i++) {
		//		if (i->m_tempo == tempo) {
		//			Erase(i);
		//			break;
		//		}
		//	}
		//}

		// ����(�b)����ʒu(position)�ƃe���|���擾
		std::pair<uint64_t, T> getPositionAndTempo(T time)const {		// return pair<step,�e���|>
			const auto i = std::upper_bound(m_list.begin(), m_list.end(), time, LessTime());
			Element tempoDefault;
			const Element& t = i == m_list.begin() ? tempoDefault : *(i - 1);
			return std::make_pair(
				t.position + static_cast<uint64_t>((time - t.time) * getCountPerSec(t.tempo)),
				t.tempo);
		}

		// �ʒu(count)���玞��(�b)���擾
		T getTime(uint64_t position)const {
			const auto i = getUpperBound(position);
			Element tempoDefault;
			const Element& t = i == m_list.begin() ? tempoDefault : *(i - 1);
			return t.time + ((position - t.position) * getSecPerCount(t.tempo));
		}

		std::pair<typename List::const_iterator, typename List::const_iterator> getEqualRange(uint64_t position)const {
			return std::make_pair(getLowerBound(position), getUpperBound(position));
		}

		const List& list()const {
			return m_list;
		}

		// 1�b������̃J�E���g�擾
		static T getCountPerSec(T tempo) {
			return tempo * timeBase / 60.0;
		}
		// 1�J�E���g������̎���(�b)�擾
		static T getSecPerCount(T tempo) {
			return static_cast<T>(60.0) / (tempo * timeBase);
		}
	private:

		// �ʒu�擾 (lower_bound:�Ώۂ̒l�ȏ�̍ŏ����w��)
		typename List::const_iterator getLowerBound(uint64_t position)const {
			return std::lower_bound(m_list.begin(), m_list.end(), position, LessPosition());
		}

		// �ʒu�擾 (upper_bound:�Ώۂ̒l�𒴂���ŏ����w��)
		typename List::const_iterator getUpperBound(uint64_t position)const {
			return std::upper_bound(m_list.begin(), m_list.end(), position, LessPosition());
		}

		void updateTime(typename List::iterator i) {	// time �X�V
			Element tempoDefault;
			Element* pBefore = i == m_list.begin() ? &tempoDefault : &*(i - 1);
			for (; i != m_list.end(); i++) {
				i->time = pBefore->time + (i->position - pBefore->position) * getSecPerCount(pBefore->tempo);
				pBefore = &*i;
			}
		}
	private:
		List	m_list;
	};

	using TempoListF = TempoListT<float>;
	using TempoList = TempoListT<double>;

}
