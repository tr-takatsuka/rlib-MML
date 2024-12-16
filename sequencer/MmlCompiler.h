#pragma once

namespace rlib::sequencer {

	class MmlCompiler
	{
		class Inner;
	public:
		static constexpr int timeBase = 480;			// 分解能(4分音符あたりのカウント)

		struct Event {
			size_t	position = 0;
			virtual ~Event() {}
			virtual std::shared_ptr<Event> clone()const = 0;
		};

		struct EventTempo : public Event {
			double	tempo = 0.0;
			virtual std::shared_ptr<Event> clone()const {
				return std::make_shared<EventTempo>(*this);
			}
		};

		struct EventMeta : public Event {
			uint8_t type = 0;
			std::string	data;
			virtual std::shared_ptr<Event> clone()const {
				return std::make_shared<EventMeta>(*this);
			}
		};

		struct EventProgramChange : public Event {
			uint8_t	programNo = 0;
			virtual std::shared_ptr<Event> clone()const {
				return std::make_shared<EventProgramChange>(*this);
			}
		};
		struct EventVolume : public Event {
			uint8_t	volume = 0;			// 音量値 0～127
			virtual std::shared_ptr<Event> clone()const {
				return std::make_shared<EventVolume>(*this);
			}
		};
		struct EventPan : public Event {
			uint8_t	pan = 0;			// パン値 0～127
			virtual std::shared_ptr<Event> clone()const {
				return std::make_shared<EventPan>(*this);
			}
		};
		struct EventPitchBend : public Event {
			int16_t	pitchBend = 0;
			virtual std::shared_ptr<Event> clone()const {
				return std::make_shared<EventPitchBend>(*this);
			}
		};
		struct EventControlChange : public Event {
			uint8_t	no = 0;				// コントロールNo 0～127
			uint8_t	value = 0;			// 値 0～127
			virtual std::shared_ptr<Event> clone()const {
				return std::make_shared<EventControlChange>(*this);
			}
		};

		struct EventNote : public Event {
			uint8_t	note = 0;			// ノート番号 0～127
			size_t	length = 0;			// 音長(ステップ数)
			uint8_t	velocity = 0;		// ベロシティ(0～127)
			virtual std::shared_ptr<Event> clone()const {
				return std::make_shared<EventNote>(*this);
			}
		};

		struct LessEvent {
			typedef void is_transparent;
			bool operator()(const std::shared_ptr<const Event>& a, const std::shared_ptr<const Event>& b)	const { return a->position < b->position; }
			bool operator()(const size_t a, const std::shared_ptr<const Event>& b)							const { return a < b->position; }
			bool operator()(const std::shared_ptr<const Event>& a, const size_t b)							const { return a->position < b; }
		};

		struct Port {
			std::string name;			// name
			std::string	instrument;		// instrument
			uint8_t	channel = 0;		// チャンネル
			std::multiset<std::shared_ptr<const Event>, LessEvent>	eventList;
		};

		using Result = std::vector<Port>;

	public:
		// parse error
		struct Exception : public std::runtime_error {
			enum class Code {
				lengthError = 1,				// 音長の指定に誤りがあります
				lengthMinusError,				// 音長を負値にはできません
				commentError,					// コメント指定に誤りがあります
				argumentError,					// 関数の引数指定に誤りがあります
				argumentUnknownError,			// 関数に不明な引数名があります
				functionCallError,				// 関数呼び出しに誤りがあります
				unknownNumberError,				// 数値の指定に誤りがあります
				vCommandError,					// ベロシティ指定（v コマンド）に誤りがあります
				vCommandRangeError,				// ベロシティ指定（v コマンド）の値が範囲外です
				lCommandError,					// デフォルト音長指定（l コマンド）に誤りがあります
				oCommandError,					// オクターブ指定（o コマンド）に誤りがあります
				oCommandRangeError,				// オクターブ指定（o コマンド）の値が範囲外です
				tCommandRangeError,				// テンポ指定（t コマンド）に誤りがあります
				programchangeCommandError,		// 音色指定（@ コマンド）に誤りがあります
				rCommandRangeError,				// 休符指定（r コマンド）に誤りがあります
				noteCommandRangeError,			// 音符指定（a～g コマンド）に誤りがあります
				octaveUpDownCommandError,		// オクターブアップダウン（ < , > コマンド）に誤りがあります
				octaveUpDownRangeCommandError,	// オクターブ値が範囲外です
				tieCommandError,				// タイ（^ コマンド）に誤りがあります
				createPortError,				// CreatePort コマンドに誤りがあります
				createPortPortNameError,		// CreatePort コマンドのポート名指定に誤りがあります
				createPortDuplicateError,		// CreatePort コマンドでポート名が重複しています
				createPortChannelError,			// CreatePort コマンドのチャンネル指定に誤りがあります
				portError,						// Port コマンドに誤りがあります
				portNameError,					// Port コマンドのポート名指定に誤りがあります
				volumeError,					// Volume コマンドの指定に誤りがあります
				volumeRangeError,				// Volume コマンドの値が範囲外です
				panError	,					// Pan コマンドの指定に誤りがあります
				panRangeError,					// Pan コマンドの値が範囲外です
				pitchBendError,					// PitchBend コマンドの指定に誤りがあります
				pitchBendRangeError,			// PitchBend コマンドの値が範囲外です
				controlChangeError,				// ControlChange コマンドの指定に誤りがあります
				controlChangeRangeError,		// ControlChange コマンドの値が範囲外です
				createSequenceError,			// CreateSequence コマンドに誤りがあります
				createSequenceDuplicateError,	// CreateSequence コマンドで名前が重複しています
				createSequenceNameError,		// CreateSequence コマンドの名前指定に誤りがあります
				sequenceError,					// Sequence コマンドに誤りがあります
				sequenceNameError,				// Sequence コマンドの名前指定に誤りがあります
				metaError,						// Meta コマンドに誤りがあります
				metaTypeError,					// Meta コマンドの type の指定に誤りがあります
				fineTuneError,					// FineTune コマンドに誤りがあります
				fineTuneRangeError,				// FineTune コマンドの値が範囲外です
				coarseTuneError,				// CoarseTune コマンドに誤りがあります
				coarseTuneRangeError,			// CoarseTune コマンドの値が範囲外です
				definePresetFMError,			// DefinePresetFM コマンドに誤りがあります
				definePresetFMNoError,			// DefinePresetFM コマンドのプログラムナンバー指定に誤りがあります
				definePresetFMRangeError,		// DefinePresetFM コマンドの値が範囲外です
				unknownError,					// 解析出来ない書式です
				stdEexceptionError,				// std::excption エラーです
			};
			const Code							code;
			const std::string::const_iterator	it;
			const std::string					errorWord;	// MML内のエラーとなった箇所の文字列
			const std::string					annotate;
			Exception(Code code_,const std::string::const_iterator& itBegin, const std::string::const_iterator& itEnd, const std::string& annotate_ = "");

			static std::string getMessage(Code code);

		};

		static Result compile(const std::string& mml);


		struct Util {

			// 単語情報
			using Word = std::variant<
				std::intmax_t,		// 符号付数値
				std::uintmax_t,		// 符号ナシ数値 ( +○○ と記述されてた場合は intmax_t )
				double,				// 浮動小数点数
				std::pair<std::string::const_iterator, std::string::const_iterator>	// 文字列
			>;

			// 単語解析
			struct ParsedWord {
				std::optional<Word>	word;		// nullならエラー
				std::string::const_iterator	next;	// 次の位置
			};
			static std::optional<ParsedWord> parseWord(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd);
		};

		static void unitTest();
	};

}
