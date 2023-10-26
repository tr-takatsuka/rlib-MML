#pragma once

namespace rlib {

	class TempoList {
	public:
		enum {
			timeBase = 480,			// ����\(4������������̃J�E���g)
		};
		struct Element {
			uint64_t	position = 0;	// �ʒu
			double		tempo = 120.0;	// �e���|
			double		time = 0.0;		// ����(�b)

			Element()
			{}
			Element(uint64_t position_, double tempo_)
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

		bool operator==(const TempoList& s)const {
			return m_list == s.m_list;
		}
		bool operator!=(const TempoList& s)const {
			return !(*this == s);
		}

		TempoList() {
		}
		TempoList(const TempoList& s)
			:m_list(s.m_list)
		{}
		TempoList(TempoList&& s)
			:m_list(std::move(s.m_list))
		{}

		void clear() {
			m_list.clear();
		}

		// Tempo �}��
		void insert(uint64_t position, double tempo) {
			const auto i = m_list.insert(												// m_listPos �ɒǉ�
				std::upper_bound(m_list.begin(), m_list.end(), position, LessPosition()),	// �Ώۂ̒l�𒴂���ŏ�
				Element(position, tempo));
			updateTime(i);	// time �X�V
		}

		// �폜
		void erase(const List::const_iterator& i) {
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
		std::pair<uint64_t, double> getPositionAndTempo(double time)const {		// return pair<step,�e���|>
			const auto i = std::upper_bound(m_list.begin(), m_list.end(), time, LessTime());
			Element tempoDefault;
			const Element& t = i == m_list.begin() ? tempoDefault : *(i - 1);
			return std::make_pair(
				t.position + static_cast<uint64_t>((time - t.time) * getCountPerSec(t.tempo)),
				t.tempo);
		}

		// �ʒu(count)���玞��(�b)���擾
		double getTime(uint64_t position)const {
			const auto i = getUpperBound(position);
			Element tempoDefault;
			const Element& t = i == m_list.begin() ? tempoDefault : *(i - 1);
			return t.time + ((position - t.position) * getSecPerCount(t.tempo));
		}

		std::pair<List::const_iterator, List::const_iterator> getEqualRange(uint64_t position)const {
			return std::make_pair(getLowerBound(position), getUpperBound(position));
		}

		const List& list()const {
			return m_list;
		}

		// 1�b������̃J�E���g�擾
		static double getCountPerSec(double tempo) {
			return tempo * timeBase / 60.0;
		}
		// 1�J�E���g������̎���(�b)�擾
		static double getSecPerCount(double tempo) {
			return 60.0 / (tempo * timeBase);
		}
	private:

		// �ʒu�擾 (lower_bound:�Ώۂ̒l�ȏ�̍ŏ����w��)
		List::const_iterator getLowerBound(uint64_t position)const {
			return std::lower_bound(m_list.begin(), m_list.end(), position, LessPosition());
		}

		// �ʒu�擾 (upper_bound:�Ώۂ̒l�𒴂���ŏ����w��)
		List::const_iterator getUpperBound(uint64_t position)const {
			return std::upper_bound(m_list.begin(), m_list.end(), position, LessPosition());
		}

		void updateTime(List::iterator i) {	// time �X�V
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

}
