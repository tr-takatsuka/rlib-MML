#pragma once

namespace rlib::sequencer {

	class Smf {
		class Inner;
		int timeBase = 480;			// 分解能(4分音符あたりのカウント)
	public:
		struct Event {
			const size_t	position;
			virtual ~Event() {}
		protected:
			Event(size_t position_)
				:position(position_)
			{}
		};

		struct EventCh : public Event {
			const uint8_t	channel;	// チャンネル 0～15
		protected:
			EventCh(size_t position, uint8_t channel_)
				:Event(position)
				, channel(channel_)
			{}
		};

		struct EventNote : public EventCh {
			const uint8_t	note;		// 0～127
			const uint8_t	velocity;	// 0～127
		protected:
			EventNote(size_t position, uint8_t channel, uint8_t note_, uint8_t velocity_)
				:EventCh(position, channel)
				, note(note_)
				, velocity(velocity_)
			{}
		};

		struct EventNoteOn : public EventNote {
			EventNoteOn(size_t position, uint8_t channel, uint8_t note, uint8_t velocity)
				:EventNote(position, channel, note, velocity)
			{}
		};

		struct EventNoteOff : public EventNote {
			EventNoteOff(size_t position, uint8_t channel, uint8_t note, uint8_t velocity)
				:EventNote(position, channel, note, velocity)
			{}
		};

		struct EventProgramChange : public EventCh {
			const uint8_t	programNo;		// 0～127
			EventProgramChange(size_t position, uint8_t channel, uint8_t programNo_)
				:EventCh(position, channel)
				, programNo(programNo_)
			{}
		};

		struct EventMeta : public Event {

			enum class Type {
				sequenceNo = 0x0,		// シーケンス番号
				text = 0x1,
				copyright = 0x2,
				sequenceName = 0x3,
				instrumentName = 0x4,
				words = 0x5,			// 歌詞
				marker = 0x6,
				channelPrefix = 0x20,
				port = 0x21,
				endOfTrack = 0x2f,
				tempo = 0x51,
				smpte = 0x54,
				meter = 0x58,
				keySignature = 0x59,	// 調号
				sequencerLocal = 0x7f,
			};
			Type						type = Type::sequenceNo;
			std::vector<uint8_t>	data;

			EventMeta(size_t position)
				:Event(position)
			{}

			static EventMeta createTempo(size_t position, double tempo);
			static EventMeta createEndOfTrack(size_t position);
		};

		struct LessEvent {
			typedef void is_transparent;
			bool operator()(const std::shared_ptr<const Event>& a, const std::shared_ptr<const Event>& b)const {
				return a->position < b->position;
			}
			bool operator()(const size_t a, const std::shared_ptr<const Event>& b)const {
				return a < b->position;
			}
			bool operator()(const std::shared_ptr<const Event>& a, const size_t b)const {
				return a->position < b;
			}
		};

		class Track {
		public:
			std::multiset<std::shared_ptr<const Event>, LessEvent>	events;
		};
	public:
		std::list<Track>	tracks;
	public:
		Smf() {}

		// SMFデータ取得
		std::vector<uint8_t> getFileImage() const;

		friend std::ostream& operator<<(std::ostream& os, const Smf& smf) {
			const auto v = smf.getFileImage();
			os.write(reinterpret_cast<const char*>(v.data()), v.size());
			return os;
		}

	};

}
