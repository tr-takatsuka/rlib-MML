#pragma once

namespace rlib::sequencer {

	class MmlCompiler {
		class Inner;
	public:
		static constexpr int timeBase = 480;			// 分解能(4分音符あたりのカウント)
	
		struct EventBase {
			virtual ~EventBase() {}
			virtual std::shared_ptr<EventBase> clone()const = 0;
		};

		struct EventSystemExclusive : public EventBase {
			std::vector<uint8_t>	data;
			virtual std::shared_ptr<EventBase> clone()const {
				return std::make_shared<EventSystemExclusive>(*this);
			}
		};

		struct EventMeta : public EventBase {
			uint8_t type = 0;
			std::vector<uint8_t> data;
			virtual std::shared_ptr<EventBase> clone()const {
				return std::make_shared<EventMeta>(*this);
			}
		};

		struct EventProgramChange : public EventBase {
			uint8_t	programNo = 0;
			virtual std::shared_ptr<EventBase> clone()const {
				return std::make_shared<EventProgramChange>(*this);
			}
		};
		struct EventPitchBend : public EventBase {
			int16_t	pitchBend = 0;
			virtual std::shared_ptr<EventBase> clone()const {
				return std::make_shared<EventPitchBend>(*this);
			}
		};
		struct EventControlChange : public EventBase {		// 注:個別イベントに当てはまらないControlChangeがコレに該当する
			uint8_t	no = 0;				// コントロールNo 0～127
			uint8_t	value = 0;			// 値 0～127
			virtual std::shared_ptr<EventBase> clone()const {
				return std::make_shared<EventControlChange>(*this);
			}
		};

		struct EventNote : public EventBase {
			uint8_t	note = 0;			// ノート番号 0～127
			size_t	length = 0;			// 音長(ステップ数)
			uint8_t	velocity = 0;		// ベロシティ(0～127)
			virtual std::shared_ptr<EventBase> clone()const {
				return std::make_shared<EventNote>(*this);
			}
		};

		struct Port {
			std::string_view	name;			// name
			std::string_view	instrument;		// instrument
			uint8_t	channel = 0;		// チャンネル
			std::multimap<size_t, std::shared_ptr<const EventBase>>	eventList;	// <position,Event>
		};
		using Event = decltype(Port::eventList)::value_type;

		enum class ErrorCode {
			lengthError = 1,				// 音長の指定に誤りがあります
			lengthMinusError,				// 音長を負値にはできません
			commentError,					// コメント指定に誤りがあります
			argumentError,					// 関数の引数指定に誤りがあります
			argumentUnknownError,			// 関数に不明な引数名があります
			functionCallError,				// 関数呼び出しに誤りがあります
			unknownNumberError,				// 数値の指定に誤りがあります
			rangeError,						// 値が範囲外です
			divideZeroError,				// 除算はゼロ以外を指定してください
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
			panError,						// Pan コマンドの指定に誤りがあります
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
			sequenceLengthError,			// Sequence コマンドの length 指定に誤りがあります
			metaError,						// Meta コマンドに誤りがあります
			metaTypeError,					// Meta コマンドの type の指定に誤りがあります
			sysExError,						// SysEx コマンドに誤りがあります
			sysExArgError,					// SysEx コマンドの引数に誤りがあります
			sysExArgFirstError,				// SysEx コマンドの先頭バイトは 0xf0 か 0xf7 を指定してください
			fineTuneError,					// FineTune コマンドに誤りがあります
			fineTuneRangeError,				// FineTune コマンドの値が範囲外です
			coarseTuneError,				// CoarseTune コマンドに誤りがあります
			coarseTuneRangeError,			// CoarseTune コマンドの値が範囲外です
			masterVolumeError,				// MasterVolume コマンドに誤りがあります
			masterVolumeRangeError,			// MasterVolume コマンドの値が範囲外です
			expressionError,				// Expression コマンドに誤りがあります
			expressionRangeError,			// Expression コマンドの値が範囲外です
			definePresetFMError,			// DefinePresetFM コマンドに誤りがあります
			definePresetFMNoError,			// DefinePresetFM コマンドのプログラムナンバー指定に誤りがあります
			definePresetFMRangeError,		// DefinePresetFM コマンドの値が範囲外です
			definePresetPSGError,			// DefinePresetPSG コマンドに誤りがあります
			definePresetPSGNoError,			// DefinePresetPSG コマンドのプログラムナンバー指定に誤りがあります
			definePresetPSGRangeError,		// DefinePresetPSG コマンドの値が範囲外です
			unknownError,					// 解析出来ない書式です
			stdEexceptionError,				// std::excption エラーです
		};


		struct Result {
			std::shared_ptr<const std::string>	mml;	// コンパイルソース(std::string_view の参照元 MML)
			std::vector<Port>	ports;
			struct Error {
				ErrorCode			code;
				std::string_view	text;
			};
			std::vector<Error>	errors;
			bool hasError() const { return errors.size() > 0; };

			static std::string getMessage(ErrorCode code);
			std::string getText(const std::vector<Error>& errors) const;
			std::string getJson(const std::vector<Error>& errors) const;
		};
		static Result compile(const std::shared_ptr<const std::string>& mml);
		static Result compile(const std::string& mml) {
			return compile(std::make_shared<const std::string>(mml));
		}


		struct Util {

			// 単語情報
			using Word = std::variant<
				std::intmax_t,		// 符号付数値
				std::uintmax_t,		// 符号ナシ数値 ( +○○ と記述されてた場合は intmax_t )
				double,				// 浮動小数点数
				std::string_view	// 文字列
			>;

			// 単語解析
			struct ParsedWord {
				std::optional<Word>	word;	// nullならエラー
				std::string_view	next;	// 次の位置
			};
			static std::optional<ParsedWord> parseWord(const std::string_view& text);
		};

		static void unitTest();
	};

}
