#pragma once

namespace rlib::sequencer {

	class MmlCompiler
	{
		static constexpr int timeBase = 480;			// 分解能(4分音符あたりのカウント)
		class Inner;
	public:
		struct Event {
			size_t	position = 0;
			virtual ~Event() {}
		};

		struct EventTempo : public Event {
			double	tempo = 0.0;
		};

		struct EventProgramChange : public Event {
			uint8_t	programNo = 0;
		};
		struct EventVolume : public Event {
			uint8_t	volume = 0;			// 音量値 0～127
		};
		struct EventPan : public Event {
			uint8_t	pan = 0;			// パン値 0～127
		};

		struct EventNote : public Event {
			uint8_t	note = 0;			// ノート番号 0～127
			size_t	length = 0;			// 音長(ステップ数)
			uint8_t	velocity = 0;		// ベロシティ(0～127)
		};

		struct LessEvent {
			typedef void is_transparent;
			bool operator()(const std::shared_ptr<const Event>& a, const std::shared_ptr<const Event>& b)	const { return a->position < b->position; }
			bool operator()(const size_t a, const std::shared_ptr<const Event>& b)							const { return a < b->position; }
			bool operator()(const std::shared_ptr<const Event>& a, const size_t b)							const { return a->position < b; }
		};

		struct Port {
			uint8_t	channel = 0;		// チャンネル
			std::multiset<std::shared_ptr<const Event>, LessEvent>	eventList;
		};

		using Result = std::vector<Port>;

	public:
		// parse error
		struct Exception : public std::runtime_error {
			enum class Code {
				lengthError = 1,			// 音長の指定に誤りがあります
				lengthMinusError,			// 音長を負値にはできません
				commentError,				// コメント指定に誤りがあります
				argumentError,				// 関数の引数指定に誤りがあります
				functionCallError,			// 関数呼び出しに誤りがあります
				unknownNumberError,			// 数値の指定に誤りがあります
				vCommandError,				// ベロシティ指定（v コマンド）に誤りがあります
				vCommandRangeError,			// ベロシティ指定（v コマンド）の値が範囲外です
				lCommandError,				// デフォルト音長指定（l コマンド）に誤りがあります
				oCommandError,				// オクターブ指定（o コマンド）に誤りがあります
				oCommandRangeError,			// オクターブ指定（o コマンド）の値が範囲外です
				tCommandRangeError,			// テンポ指定（t コマンド）に誤りがあります
				programchangeCommandError,	// 音色指定（@ コマンド）に誤りがあります
				rCommandRangeError,			// 休符指定（r コマンド）に誤りがあります
				noteCommandRangeError,		// 音符指定（a～g コマンド）に誤りがあります
				octaveUpDownCommandError,	// オクターブアップダウン（ < , > コマンド）に誤りがあります
				octaveUpDownRangeCommandError,	// オクターブ値が範囲外です
				tieCommandError,			// タイ（^ コマンド）に誤りがあります
				createPortPortNameError,	// createPort コマンドのポート名指定に誤りがあります
				createPortDuplicateError,	// createPort コマンドでポート名が重複しています
				createPortChannelError,		// createPort コマンドのチャンネル指定に誤りがあります
				portNameError,				// port コマンドのポート名指定に誤りがあります
				volumeError,				// volume コマンドの指定に誤りがあります
				volumeRangeError,			// volume コマンドの値が範囲外です
				panError	,				// pan コマンドの指定に誤りがあります
				panRangeError,				// pan コマンドの値が範囲外です
				unknownError,				// 解析出来ない書式です
				stdEexceptionError,			// std::excption エラーです
			};
			const Code							code;
			const std::string::const_iterator	it;
			const std::string					errorWord;	// MML内のエラーとなった箇所の文字列
			const std::string					annotate;
			Exception(Code code_,const std::string::const_iterator& itBegin, const std::string::const_iterator& itEnd, const std::string& annotate_ = "");

			static std::string getMessage(Code code);

		};

		static Result compile(const std::string& mml);

	};

}
