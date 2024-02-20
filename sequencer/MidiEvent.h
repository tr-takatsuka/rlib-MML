#pragma once

namespace rlib::midi {

	namespace inner {
#pragma pack( push )
#pragma pack( 1 )
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

			VariableByte(uint8_t val=0)
				:value(val)
			{}
			VariableByte(uint8_t b7_, bool flag_)
				:b7(b7_)
				, flag(flag_)
			{}
		};
#pragma pack( pop )

		// 可変長数値作成
		static std::vector<uint8_t> getVariableValue(uint64_t n) {
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

		// 可変長数値から値を取得
		static uint64_t readVariableValue(const std::function<uint8_t()>& fReadByte) {
			inner::VariableValue vv;
			for (size_t i = 0; i < 8; i++) {	// failsafe
				const VariableByte vb(fReadByte());
				vv.b7 = vv.b6;
				vv.b6 = vv.b5;
				vv.b5 = vv.b4;
				vv.b4 = vv.b3;
				vv.b3 = vv.b2;
				vv.b2 = vv.b1;
				vv.b1 = vv.b0;
				vv.b0 = vb.b7;
				if (!vb.flag) return vv.value;
			}
			throw std::runtime_error("variable value error");
		};

	}

	struct Event {
		virtual ~Event() {}
		virtual std::vector<uint8_t> midiMessage() const = 0;
	};

	struct EventCh : public Event {
		const uint8_t	channel = 0;	// チャンネル 0～15
	protected:
		EventCh(uint8_t channel_)
			:channel(channel_)
		{}
	};

	struct EventNote : public EventCh {
		const uint8_t	note = 0;		// 0～127
		const uint8_t	velocity = 0;	// 0～127
	protected:
		EventNote(uint8_t channel, uint8_t note_, uint8_t velocity_)
			:EventCh(channel), note(note_), velocity(velocity_)
		{}
	};

	struct EventNoteOff : public EventNote {
		static constexpr uint8_t statusByte = 0x80;
		EventNoteOff(uint8_t channel, uint8_t note, uint8_t velocity)
			:EventNote(channel, note, velocity)
		{}
		virtual std::vector<uint8_t> midiMessage() const {
			return std::vector<uint8_t>{static_cast<uint8_t>(statusByte | (channel & 0xf)), static_cast<uint8_t>(note & 0x7f), static_cast<uint8_t>(velocity & 0x7f)};
		}
	};

	struct EventNoteOn : public EventNote {
		static constexpr uint8_t statusByte = 0x90;
		EventNoteOn(uint8_t channel, uint8_t note, uint8_t velocity)
			:EventNote(channel, note, velocity)
		{}
		virtual std::vector<uint8_t> midiMessage() const {
			return std::vector<uint8_t>{static_cast<uint8_t>(statusByte | (channel & 0xf)), static_cast<uint8_t>(note & 0x7f), static_cast<uint8_t>(velocity & 0x7f)};
		}
	};

	struct EventPolyphonicKeyPressure : public EventCh {
		static constexpr uint8_t statusByte = 0xa0;
		const uint8_t	note = 0;		// 0～127
		const uint8_t	pressure = 0;	// 0～127
		EventPolyphonicKeyPressure(uint8_t channel, uint8_t note_, uint8_t pressure_)
			:EventCh(channel), note(note_), pressure(pressure_)
		{}
		virtual std::vector<uint8_t> midiMessage() const {
			return std::vector<uint8_t>{static_cast<uint8_t>(statusByte | (note & 0xf)), static_cast<uint8_t>(pressure & 0x7f)};
		}
	};

	struct EventControlChange : public EventCh {
		static constexpr uint8_t statusByte = 0xb0;
		enum class Type {
			bankselectMSB = 0,
			modulation = 1,
			volume = 7,
			pan = 10,
			expression = 11,
			bankselectLSB = 32,
		};
		const Type		type = static_cast<Type>(0);
		const uint8_t	value = 0;
		EventControlChange(uint8_t channel, Type type_, uint8_t value_)
			:EventCh(channel), type(type_), value(value_)
		{}
		EventControlChange(uint8_t channel, uint8_t type_, uint8_t value_)
			:EventCh(channel), type(static_cast<Type>(type_)), value(value_)
		{}
		virtual std::vector<uint8_t> midiMessage() const {
			return std::vector<uint8_t>{static_cast<uint8_t>(statusByte | (channel & 0xf)), static_cast<uint8_t>(static_cast<uint8_t>(type) & 0x7f), static_cast<uint8_t>(value & 0x7f)};
		}
	};

	struct EventProgramChange : public EventCh {
		static constexpr uint8_t statusByte = 0xc0;
		const uint8_t	programNo = 0;		// 0～127
		EventProgramChange(uint8_t channel, uint8_t programNo_)
			:EventCh(channel), programNo(programNo_)
		{}
		virtual std::vector<uint8_t> midiMessage() const {
			return std::vector<uint8_t>{static_cast<uint8_t>(statusByte | (channel & 0xf)), static_cast<uint8_t>(programNo & 0x7f)};
		}
	};

	struct EventPitchBend : public EventCh {
		static constexpr uint8_t statusByte = 0xe0;
		const int16_t	pitchBend;			// -8192 ～ 8191
		EventPitchBend(uint8_t channel, int16_t pitchBend_)
			:EventCh(channel), pitchBend(pitchBend_)
		{}
		virtual std::vector<uint8_t> midiMessage() const {
			const int n = pitchBend + 8192;
			return std::vector<uint8_t>{static_cast<uint8_t>(statusByte | (channel & 0xf)), static_cast<uint8_t>(n & 0x7f), static_cast<uint8_t>(n / 0x80 & 0x7f)};
		}
	};

	struct EventChannelPressure : public EventCh {
		static constexpr uint8_t statusByte = 0xd0;
		const uint8_t	channelPressure = 0;		// 0～127
		EventChannelPressure(uint8_t channel, uint8_t channelPressure_)
			:EventCh(channel), channelPressure(channelPressure_)
		{}
		virtual std::vector<uint8_t> midiMessage() const {
			return std::vector<uint8_t>{static_cast<uint8_t>(statusByte | (channel & 0xf)), static_cast<uint8_t>(channelPressure & 0x7f)};
		}
	};

	struct EventExclusive : public Event {
		static constexpr uint8_t statusByte = 0xf0;
		const std::vector<uint8_t>	data;
		EventExclusive(std::vector<uint8_t>&& data_)
			:Event(), data(std::move(data_))
		{}
		virtual std::vector<uint8_t> midiMessage() const {
			std::vector<uint8_t> v;
			v.push_back(statusByte);
			v.insert(v.end(), data.begin(), data.end());
			v.push_back(static_cast<unsigned char>(0xf7));			// 終了コード
			return v;
		}
	};

	struct EventMeta : public Event {
		static constexpr uint8_t statusByte = 0xff;
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
		const Type					type = Type::sequenceNo;
		const std::vector<uint8_t>	data;

		EventMeta(Type type_, std::vector<uint8_t>&& data_)
			:Event()
			, type(type_)
			, data(std::move(data_))
		{}

		virtual std::vector<uint8_t> midiMessage() const {
			std::vector<uint8_t> v{
				statusByte,
				static_cast<uint8_t>(type),
			};
			{// データサイズ
				std::vector<uint8_t> s = inner::getVariableValue(data.size());
				v.insert(v.end(), std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
			}
			v.insert(v.end(), data.begin(), data.end());	// データ
			return v;
		}

		double getTempo()const {
			if (type != Type::tempo) throw std::logic_error("invalid type");
			union {
				uint8_t		buf[4] = { 0, };
				uint32_t	tempo;
			}t;
			for (size_t i = 0; i < (std::min<size_t>)(data.size(), 3); i++) {
				t.buf[i + 1] = data[i];
			}
			std::reverse(reinterpret_cast<uint8_t*>(&t.tempo), reinterpret_cast<uint8_t*>(&t.tempo) + sizeof(t.tempo));		// エンディアン変更
			return 60000000.0 / t.tempo;
		}

		static EventMeta createTempo(double tempo) {
			union {
				uint8_t		buf[4] = { 0, };
				uint32_t	tempo;
			}t;
			assert(sizeof(t) == 4);
			t.tempo = static_cast<uint32_t>(60000000.0 / tempo);		// 4分音符の時間(microsec)
			std::reverse(reinterpret_cast<uint8_t*>(&t.tempo), reinterpret_cast<uint8_t*>(&t.tempo) + sizeof(t.tempo));		// エンディアン変更
			return EventMeta(Type::tempo, { t.buf[1], t.buf[2], t.buf[3] });
		}

		static EventMeta createEndOfTrack() {
			return EventMeta(Type::endOfTrack, {});
		}
	};

}
