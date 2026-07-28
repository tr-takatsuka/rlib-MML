

#include <regex>
#include <charconv>
#include <variant>
#include <optional>
#include <iostream>
#include <typeindex>


#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>

#include "../json/Json.h"
#include "../stringformat/StringFormat.h"

#include "./MmlCompiler.h"
#include "./MidiEvent.h"

using namespace rlib;
using namespace rlib::sequencer;

namespace {

	// parse error
	struct MmlException : public std::runtime_error {
		const std::vector<MmlCompiler::Result::Error> errors;
		MmlException(MmlCompiler::ErrorCode code, const std::string_view& text)
			:std::runtime_error(MmlCompiler::Result::getMessage(code))
			, errors({ {code,text} })
		{}
		MmlException(std::vector<MmlCompiler::Result::Error>&& errors)
			:std::runtime_error(errors.size() > 0 ? MmlCompiler::Result::getMessage(errors[0].code) : "")
			, errors(std::move(errors))
		{}
	};

	auto getLineColumn(const std::string& text, const std::string_view& target) {
		struct {
			size_t line = 1;
			size_t column = 1;
		}result;
		const std::string_view whole = text;
		size_t offset = static_cast<size_t>(target.data() - whole.data());
		for (size_t i = 0; i < offset; i++) {
			unsigned char c = static_cast<unsigned char>(whole[i]);
			if (c == '\r') {
				result.line++;
				result.column = 1;
				if (i + 1 < offset && whole[i + 1] == '\n') i++;	// 次が '\n' なら飛ばす（CRLF）
			} else if (c == '\n') {
				result.line++;
				result.column = 1;
			} else if ((c & 0xc0) != 0x80) { // UTF-8 継続バイト以外(=文字開始)なら
				result.column++;
			}
		}
		return result;
	};

	// 文字列から先頭の文字数を取得(エラー箇所の文字列を取得用)
	const auto getTextPrefix = [](const std::string_view& text, size_t length = 8) -> std::string_view {
		size_t i = 0;
		for (size_t column = 0; i < text.size(); i++) {
			unsigned char c = static_cast<unsigned char>(text[i]);
			if (c == '\r' || c == '\n') break;	// 改行があれば終了
			if ((c & 0xc0) != 0x80) {	// UTF-8 継続バイト以外(=文字開始)なら
				if (++column > length) break;
			}
		}
		return text.substr(0, i);
	};

}

std::string MmlCompiler::Result::getMessage(ErrorCode code) {
	static const std::map<ErrorCode, std::u8string> m = {
		{ErrorCode::lengthError,					u8R"(音長の指定に誤りがあります)"},
		{ErrorCode::lengthMinusError,				u8R"(音長を負値にはできません)"},
		{ErrorCode::commentError,					u8R"(コメント指定に誤りがあります)"},
		{ErrorCode::argumentError,					u8R"(関数の引数指定に誤りがあります)"},
		{ErrorCode::argumentUnknownError,			u8R"(関数に不明な引数名があります)"},
		{ErrorCode::functionCallError,				u8R"(関数呼び出しに誤りがあります)"},
		{ErrorCode::unknownNumberError,				u8R"(数値の指定に誤りがあります)"},
		{ErrorCode::rangeError,						u8R"(値が範囲外です)"},
		{ErrorCode::divideZeroError,				u8R"(除算はゼロ以外を指定してください)"},
		{ErrorCode::vCommandError,					u8R"(ベロシティ指定（v コマンド）に誤りがあります)"},
		{ErrorCode::vCommandRangeError,				u8R"(ベロシティ指定（v コマンド）の値が範囲外です)"},
		{ErrorCode::lCommandError,					u8R"(デフォルト音長指定（l コマンド）に誤りがあります)"},
		{ErrorCode::oCommandError,					u8R"(オクターブ指定（o コマンド）に誤りがあります)"},
		{ErrorCode::oCommandRangeError,				u8R"(オクターブ指定（o コマンド）の値が範囲外です)"},
		{ErrorCode::tCommandRangeError,				u8R"(テンポ指定（t コマンド）に誤りがあります)"},
		{ErrorCode::programchangeCommandError,		u8R"(音色指定（@ コマンド）に誤りがあります)"},
		{ErrorCode::rCommandRangeError,				u8R"(休符指定（r コマンド）に誤りがあります)"},
		{ErrorCode::noteCommandRangeError,			u8R"(音符指定（a～g コマンド）に誤りがあります)"},
		{ErrorCode::octaveUpDownCommandError,		u8R"(オクターブアップダウン（ < , > コマンド）に誤りがあります)"},
		{ErrorCode::octaveUpDownRangeCommandError,	u8R"(オクターブ値が範囲外です)"},
		{ErrorCode::tieCommandError,				u8R"(タイ（^ コマンド）に誤りがあります)"},
		{ErrorCode::createPortError,				u8R"(CreatePort コマンドに誤りがあります)"},
		{ErrorCode::createPortPortNameError,		u8R"(CreatePort コマンドのポート名指定に誤りがあります)"},
		{ErrorCode::createPortDuplicateError,		u8R"(CreatePort コマンドでポート名が重複しています)"},
		{ErrorCode::createPortChannelError,			u8R"(CreatePort コマンドのチャンネル指定に誤りがあります)"},
		{ErrorCode::portError,						u8R"(Port コマンドに誤りがあります)"},
		{ErrorCode::portNameError,					u8R"(Port コマンドのポート名指定に誤りがあります)"},
		{ErrorCode::volumeError,					u8R"(Volume コマンドの指定に誤りがあります)"},
		{ErrorCode::volumeRangeError,				u8R"(Volume コマンドの値が範囲外です)"},
		{ErrorCode::panError,						u8R"(Pan コマンドの指定に誤りがあります)"},
		{ErrorCode::panRangeError,					u8R"(Pan コマンドの値が範囲外です)"},
		{ErrorCode::pitchBendError,					u8R"(PitchBend コマンドの指定に誤りがあります)"},
		{ErrorCode::pitchBendRangeError,			u8R"(PitchBend コマンドの値が範囲外です)"},
		{ErrorCode::controlChangeError,				u8R"(ControlChange コマンドの指定に誤りがあります)"},
		{ErrorCode::controlChangeRangeError,		u8R"(ControlChange コマンドの値が範囲外です)"},
		{ErrorCode::createSequenceError,			u8R"(CreateSequence コマンドに誤りがあります)"},
		{ErrorCode::createSequenceDuplicateError,	u8R"(CreateSequence コマンドで名前が重複しています)"},
		{ErrorCode::createSequenceNameError,		u8R"(CreateSequence コマンドの名前指定に誤りがあります)"},
		{ErrorCode::sequenceError,					u8R"(Sequence コマンドに誤りがあります)"},
		{ErrorCode::sequenceNameError,				u8R"(Sequence コマンドの名前指定に誤りがあります)"},
		{ErrorCode::sequenceLengthError,			u8R"(Sequence コマンドの length 指定に誤りがあります)"},
		{ErrorCode::metaError,						u8R"(Meta コマンドに誤りがあります)"},
		{ErrorCode::metaTypeError,					u8R"(Meta コマンドの type の指定に誤りがあります)"},
		{ErrorCode::sysExError,						u8R"(SysEx コマンドに誤りがあります)"},
		{ErrorCode::sysExArgError,					u8R"(SysEx コマンドの引数に誤りがあります)"},
		{ErrorCode::sysExArgFirstError,				u8R"(SysEx コマンドの先頭バイトは 0xf0 か 0xf7 を指定してください)"},
		{ErrorCode::fineTuneError,					u8R"(FineTune コマンドの指定に誤りがあります)"},
		{ErrorCode::fineTuneRangeError,				u8R"(FineTune コマンドの値が範囲外です)"},
		{ErrorCode::coarseTuneError,				u8R"(CoarseTune コマンドの指定に誤りがあります)"},
		{ErrorCode::coarseTuneRangeError,			u8R"(CoarseTune コマンドの値が範囲外です)"},
		{ErrorCode::masterVolumeError,				u8R"(MasterVolume コマンドに誤りがあります)"},
		{ErrorCode::masterVolumeRangeError,			u8R"(MasterVolume コマンドの値が範囲外です)"},
		{ErrorCode::expressionError,				u8R"(Expression コマンドに誤りがあります)"},
		{ErrorCode::expressionRangeError,			u8R"(Expression コマンドの値は 0～127 の正数あるいは相対値を指定してください)"},
		{ErrorCode::definePresetFMError,			u8R"(DefinePresetFM コマンドに誤りがあります)"},
		{ErrorCode::definePresetFMNoError,			u8R"(DefinePresetFM コマンドのプログラムナンバー指定に誤りがあります)"},
		{ErrorCode::definePresetFMRangeError,		u8R"(DefinePresetFM コマンドの値が範囲外です)"},
		{ErrorCode::definePresetPSGError,			u8R"(DefinePresetPSG コマンドに誤りがあります)"},
		{ErrorCode::definePresetPSGNoError,			u8R"(DefinePresetPSG コマンドのプログラムナンバー指定に誤りがあります)"},
		{ErrorCode::definePresetPSGRangeError,		u8R"(DefinePresetPSG コマンドの値が範囲外です)"},
		{ErrorCode::unknownError,					u8R"(解析出来ない書式です)"},
		{ErrorCode::stdEexceptionError,				u8R"(std::excption エラーです)"},
	};
	if (auto i = m.find(code); i != m.end()) {
		return std::string(reinterpret_cast<const char*>(i->second.data()), i->second.size());
	}
	assert(false);
	return "unknown";
}

std::string MmlCompiler::Result::getText(const std::vector<Result::Error>& errors) const {
	std::string result;
	for (const auto& err : errors) {
		const auto pos = getLineColumn(*mml, err.text);				// 行と列を取得
		const auto msg = MmlCompiler::Result::getMessage(err.code);	// エラーメッセージ取得
		const auto errorText = getTextPrefix(err.text, 10);			// エラー箇所のMML
		result += result.empty() ? "" : "\n";
		result += string::format("%d:%d error %03d: %s '%s'", pos.line, pos.column, static_cast<size_t>(err.code), msg, errorText);
	}
	return result;
}

std::string MmlCompiler::Result::getJson(const std::vector<Result::Error>& errors) const {
	Json json;
	auto& list = json.ensureMap()["errors"].ensureArray();
	for (const auto& err : errors) {
		const auto pos = getLineColumn(*mml, err.text);				// 行と列を取得
		const auto msg = MmlCompiler::Result::getMessage(err.code);	// エラーメッセージ取得
		const auto errorText = getTextPrefix(err.text, 10);			// エラー箇所のMML
		list.push_back(
			Json::Map{
				{"code",	static_cast<size_t>(err.code)},
				{"line",	pos.line},
				{"column",	pos.column},
				{"message",	string::format("%s '%s'", msg, errorText)},
			});
	}
	return json.stringify();
}


namespace {

	using regex = boost::regex;

	auto regexSearch(const std::string_view& text, const regex& re) {
		// match_not_dot_newline  : . は改行以外にマッチさせる。(std::regexと同じにする)
		// match_single_line	  : ^ は先頭行の行頭のみにマッチ
		// boost::match_continuous: 先頭から始まる部分シーケンスにのみマッチすることを指定する
		boost::match_results<std::string_view::const_iterator> m;
		if (boost::regex_search(text.begin(), text.end(), m, re, boost::match_not_dot_newline | boost::match_single_line | boost::match_continuous)) {
			return std::optional(m);
		}
		return std::optional<decltype(m)>();
	}

	// 文字列の前方一致比較
	std::optional<std::string_view> isStartsWith(const std::string_view& text, const std::string_view& prefix) {
		if (text.starts_with(prefix)) {
			return std::string_view(text.begin() + prefix.size(), text.end());
		}
		return std::nullopt;
	};
	std::optional<std::string_view> isStartsWith(const std::string_view& text, const char prefix) {
		if (text.starts_with(prefix)) {
			return std::string_view(text.begin() + 1, text.end());
		}
		return std::nullopt;
	};

	// 文字列リテラルパース
	auto parseString(const std::string_view& text) {
		struct Result {
			std::string_view next;					// 次の位置
			std::optional<std::string_view> value;	// パースした文字列 nullの場合はパースエラー(終端がない)
		};
		std::optional<Result> result(Result{ text });
		if (result->next.starts_with('"')) {						// "・・・" 形式の文字列
			result->next.remove_prefix(1);							// 先頭の " を飛ばす
			auto pos = result->next.find('"');						// 終端の " を探す
			if (pos == std::string_view::npos) return result;		// 終端が見つからないならパースエラー					
			result->value = result->next.substr(0, pos);			// パースした文字列
			result->next = result->next.substr(pos + 1);			// 終端の " の次
			return result;
		}
		if (result->next.starts_with('R')) {					// R"xx(・・・)xx" 形式の文字列
			static const regex re(R"(^R\"(\w*)\()");
			const auto m = regexSearch(result->next, re);
			if (!m) return std::optional<Result>();					// 対象外(エラーではない)
			const auto& delimiter = (*m)[1];
			const auto iString = (*m)[0].second;					// 文字列開始位置
			result->next.remove_prefix(std::distance(result->next.begin(), iString));	// 文字列開始位置まで進める
			const auto sEnd = ")" + delimiter + "\"";				// 終端文字列
			auto pos = result->next.find(sEnd);						// 終端を検索
			if (pos == std::string_view::npos) return result;		// 終端が見つからないならパースエラー
			result->value = result->next.substr(0, pos);			// パースした文字列
			result->next = result->next.substr(pos + sEnd.size());	// 終端の次
			return result;
		}
		return std::optional<Result>();							// 対象外
	};

	// コメント解析（コメントと空白と改行を読み飛ばす）
	std::string_view skipComment(const std::string_view& mml) {
		auto next = mml;
		while (true) {
			// 先頭の空白を読み飛ばす
			next.remove_prefix(std::distance(next.begin(), std::find_if(next.begin(), next.end(), [](unsigned char ch) {
				return !std::isspace(ch); // return !std::isspace(ch, std::locale::classic()); gccでNG
			})));

			if (const auto r = isStartsWith(next, "//")) {	// "//" コメント?
				const size_t pos = r->find_first_of("\r\n");	// 終了位置(改行)検索
				if (pos == std::string_view::npos) {
					next = std::string_view{};					// 次位置は空
				} else {
					size_t skip = 1;
					if ((*r)[pos] == '\r' && pos + 1 < r->size() && (*r)[pos + 1] == '\n') skip = 2;	// CRLF対応
					next = r->substr(pos + skip);
				}
				continue;
			}

			if (const auto r = isStartsWith(next, "/*")) {	// "/*" コメント?
				const auto pos = r->find("*/");					// 終了位置(改行)検索
				if (pos == std::string_view::npos) throw MmlException(MmlCompiler::ErrorCode::commentError, next);	// 見つからないならエラー
				next = r->substr(pos + 2);						// "*/" の次
				continue;
			}

			break;	// コメントではないなら抜ける
		}
		return next;
	};

	// 整数パース
	auto parseInt(const std::string_view& text) {
		struct Result {
			std::string_view					next;
			std::variant<intmax_t, uintmax_t>	value;	// 正数:uintmax_t 負数:intmax_t +○○:intmax_t
		};

		// 16進数
		if (const auto r = isStartsWith(text, "0x")) {
			uintmax_t value;
			const auto [ptr, ec] = std::from_chars(r->data(), r->data() + r->size(), value, 16);
			if (ec != std::errc{}) return std::optional<Result>();
			return std::optional(Result{ std::string_view(ptr, r->data() + r->size() - ptr), value });
		}

		auto next = text;
		int sign = 0;
		if (const auto r = isStartsWith(next, '-')) {
			sign = -1;
			next = *r;
		} else if (const auto r = isStartsWith(next, '+')) {
			sign = 1;
			next = *r;
		}
		uintmax_t value;
		const auto [ptr, ec] = std::from_chars(next.data(), next.data() + next.size(), value, 10);
		if (ec != std::errc{}) return std::optional<Result>();
		Result result;
		if (sign < 0) {
			result.value = -static_cast<intmax_t>(value);
		} else if (sign > 0) {
			result.value = static_cast<intmax_t>(value);
		} else {
			result.value = static_cast<uintmax_t>(value);
		}
		result.next = std::string_view(ptr, next.data() + next.size() - ptr);
		return std::optional(result);
	}

	// 浮動小数点数パース
	auto parseDouble(const std::string_view& text) {
		struct Result {
			std::string_view	next;
			double				value;
		};
#if 0
		// emscripten では std::from_chars の double 版が使えない "call to deleted function 'from_chars'"
		const auto [begin, end] = toAddress(iBegin, iEnd);
		const auto [ptr, ec] = std::from_chars(begin, end, result.value, std::chars_format::fixed);	// 指数表記は不許可
		if (ec != std::errc{}) return std::optional<decltype(result)>();
		result.next = iBegin + (ptr - begin);
		return std::optional(result);
#else
		if (const auto m = regexSearch(text, regex(R"(^([0-9]+(\.[0-9]*)?))"))) {	// regex(R"(^(\+?[0-9]+(\.[0-9]*)?([eE][\+-]?[0-9]+)?))"
			Result result;
			result.value = boost::lexical_cast<double>((*m)[1].str());
			result.next = std::string_view((*m)[0].second, text.end());
			return std::optional(result);
		}
		return std::optional<Result>();
#endif
	}


	// 数値パース
	struct ParseNum {
		enum class Ope {
			Set, Add, Sub, Mul, Div
		};
		struct Assign {
			std::string_view	next;
			std::string_view	matched;
			Ope					ope = Ope::Set;
			std::variant<intmax_t, double>	value;

			template <typename T=double> T  getValue()const {
				return std::visit([](auto v) {
					return static_cast<T>(v);
				}, value);
			}

			template <typename T> T apply(T base)const {
				assert(std::isfinite(base));
				return std::visit([&](auto&& v) -> T {
					using V = std::decay_t<decltype(v)>;
					if constexpr (std::is_integral_v<T> && std::is_integral_v<V>) { // 整数版
						switch (ope) {
						case Ope::Set: return static_cast<T>(v);
						case Ope::Add: return static_cast<T>(base + v);
						case Ope::Sub: return static_cast<T>(base - v);
						case Ope::Mul: return static_cast<T>(base * v);
						case Ope::Div: return static_cast<T>(base / v); // 補足:ゼロ割りはParse時にチェック済
						}
					} else {		// 浮動小数点版
						double db = static_cast<double>(base);
						double dv = static_cast<double>(v);
						switch (ope) {
						case Ope::Set: return static_cast<T>(dv);
						case Ope::Add: return static_cast<T>(db + dv);
						case Ope::Sub: return static_cast<T>(db - dv);
						case Ope::Mul: return static_cast<T>(db * dv);
						case Ope::Div: return static_cast<T>(db / dv);	 // 補足:ゼロ割りはParse時にチェック済
						}
					}
					assert(false);
					return base;
				}, value);
			}

		};
		static std::optional<Assign> parse(
			const std::string_view& text,
			bool offsetMode = false	// true:符号付は相対指定とする（例："+1"=Add,1.0、"-1.5"=Sub,1.5） false:（例："+1"=Set,1.0、"-1.5"=Ope::Set,-1.5）
		) {
			auto assign = [&]()-> std::optional<Assign> {
				Assign as(text);
				if (const auto r = isStartsWith(as.next, "+=")) {
					as.next = *r;
					as.ope = Ope::Add;
				} else if (const auto r = isStartsWith(as.next, "-=")) {
					as.next = *r;
					as.ope = Ope::Sub;
				} else if (const auto r = isStartsWith(as.next, "*=")) {
					as.next = *r;
					as.ope = Ope::Mul;
				} else if (const auto r = isStartsWith(as.next, "/=")) {
					as.next = *r;
					as.ope = Ope::Div;
				}
				int signedMark = 0;
				if (const auto r = isStartsWith(as.next, '+')) {
					as.next = *r;		// msvcのfrom_charsは "+1" がパース不可("+1.1","-1"はOK)なので、その意味でもこの処理は必要
					signedMark = 1;
				} else if (const auto r = isStartsWith(as.next, '-')) {
					as.next = *r;
					signedMark = -1;
				}
				if (signedMark != 0) {
					if (isStartsWith(as.next, '+') || isStartsWith(as.next, '-')) return std::nullopt;	// 符号が連続してるならエラー
					if (as.ope == Ope::Set && offsetMode) {
						as.ope = signedMark > 0 ? Ope::Add : Ope::Sub;
						signedMark = 0;
					}
				}

				// 16進数
				if (const auto r = isStartsWith(as.next, "0x")) {
					as.next = *r;
					uintmax_t value;
					const auto [ptr, ec] = std::from_chars(as.next.data(), as.next.data() + as.next.size(), value, 16);
					if (ec != std::errc{}) return std::nullopt;
					as.matched = std::string_view(text.data(), ptr - text.data());
					as.next = std::string_view(ptr, as.next.data() + as.next.size() - ptr);
					as.value = static_cast<intmax_t>(value) * (signedMark < 0 ? -1 : 1);
					return as;
				}

				double value;
				const auto [ptr, ec] = std::from_chars(as.next.data(), as.next.data() + as.next.size(), value, std::chars_format::fixed); // fixed:指数表記(e)を禁止 "v76e5" のようなケースを考慮。 
				if (ec != std::errc()) return std::nullopt;
				const char* p = as.next.data();
				for (; p < ptr; p++) {
					if (*p == '.') { // 指数表記を禁止してるので 'e','E' は考慮しない
						as.value = value * (signedMark < 0 ? -1 : 1);
						break;
					}
				}
				if (p >= ptr) as.value = static_cast<intmax_t>(value) * (signedMark < 0 ? -1 : 1);
				as.matched = std::string_view(text.data(), ptr - text.data());
				as.next = std::string_view(ptr, as.next.data() + as.next.size() - ptr);
				return as;
			}();

			if (assign && assign->ope == Ope::Div) {	// ゼロ割りチェック
				std::visit([&](auto&& v) {
					using V = std::decay_t<decltype(v)>;
					if constexpr (std::is_floating_point_v<V>) {
						if (v == static_cast<V>(0.0)) throw MmlException(MmlCompiler::ErrorCode::divideZeroError, assign->matched);
					} else {
						if (v == 0) throw MmlException(MmlCompiler::ErrorCode::divideZeroError, assign->matched);
					}
				}, assign->value);
			}

			return assign;
		}
	};

	// 関数解析
	struct ParseFunctionResult {
		std::string_view								functionName;		// 関数名
		std::string_view								next;				// 次の位置
		std::vector<MmlCompiler::Util::Word>			argsList;			// 引数リスト(名前ナシ連番)
		std::map<std::string, MmlCompiler::Util::Word>	argsName;			// 引数リスト(名前アリ)

		template <typename T> std::pair<bool, std::optional<T>> findArg(const std::string& name)const {
			if (auto i = argsName.find(name); i != argsName.end()) {
				if (auto n = std::get_if<T>(&i->second)) {
					return std::make_pair(true, *n);
				}
				return std::make_pair(true, std::nullopt);	// 型が違う
			}
			return std::make_pair(false, std::nullopt);
		}
		template <typename T> std::pair<bool, std::optional<T>> findArg(size_t index)const {
			if (argsList.size() > index) {
				if (auto n = std::get_if<T>(&argsList[index])) {
					return std::make_pair(true, *n);
				}
				return std::make_pair(true, std::nullopt);	// 型が違う
			}
			return std::make_pair(false, std::nullopt);
		}

		template <typename T> std::optional<std::string_view> findArgString(const T& name)const {
			if (auto r = findArg<std::string_view>(name).second) {
				return r;
			}
			return std::nullopt;
		}

		template <typename T> std::optional<std::intmax_t> findArgInt(const T& name)const {
			if (auto r = findArg<std::intmax_t>(name).second) {
				return r;
			}
			if (auto r = findArg<std::uintmax_t>(name).second) {
				return r;
			}
			return std::nullopt;
		}

		// 整数でも浮動小数でも有効とする
		template <typename T> std::optional<double> findArgNumber(const T& name)const {
			if (auto r = findArg<std::intmax_t>(name).second) {
				return static_cast<double>(*r);
			}
			if (auto r = findArg<std::uintmax_t>(name).second) {
				return static_cast<double>(*r);
			}
			if (auto r = findArg<double>(name).second) {
				return r;
			}
			return std::nullopt;
		}

	};

	auto parseFunction(
		const std::string_view& text,
		const std::vector<std::string>& functionNames,				// 関数名
		const std::set<std::string>& argNames,						// 名前付き引数名(ココにない引数名はエラー)
		const size_t argCount = (std::numeric_limits<size_t>::max)()	// 名前ナシ引数数(ココを超える数の引数はエラー)
	) {
		ParseFunctionResult result;
		auto arg = [&]()->std::optional<std::string_view> {	// 
			for (auto& name : functionNames) {
				const auto r = isStartsWith(text, name);
				if (!r) continue;
				auto next = skipComment(*r);		// コメントを読み飛ばす
				const auto r2 = isStartsWith(next, '(');	// 関数の"(" を確認
				if (!r2) continue;
				result.functionName = { text.begin(), text.begin() + name.size() };
				return *r2;
			}
			return std::nullopt;
		}();
		if (!arg) return std::optional<ParseFunctionResult>();		// 該当しなかった
		auto next = *arg;

		std::string currentArgName;
		union {									// 次トークン情報
			struct {
				uint16_t	rightParen : 1;		// )
				uint16_t	comma : 1;			// ,
				uint16_t	argName : 1;		// 引数名
				uint16_t	argValue : 1;		// 引数値
			};
			uint16_t		all = 0;
		}flags;
		flags.rightParen = true;
		flags.argName = true;
		flags.argValue = true;

		while (true) {
			next = skipComment(next);		// コメントを読み飛ばす
			if (next.empty()) break;

			if (flags.rightParen) {
				if (const auto r = isStartsWith(next, ')')) {		// ")"
					result.next = *r;									// 次の位置
					return std::optional(result);						// 正常終了
				}
			}

			if (flags.comma) {
				if (const auto r = isStartsWith(next, ',')) {		// ","
					next = *r;
					flags.all = 0;
					flags.rightParen = true;
					flags.argName = true;
					flags.argValue = true;
					continue;
				}
			}

			if (flags.argName) {		// 引数名
				static const auto re = regex(R"(^([a-zA-Z]\w*))");
				if (const auto m = regexSearch(next, re)) {
					auto n = std::string_view((*m)[0].second, next.end());
					n = skipComment(n);			// コメントを読み飛ばす
					if (const auto r = isStartsWith(n, ':')) {		// ":" があれば引数名で確定
						currentArgName = (*m)[0].str();
						if (auto i = argNames.find(currentArgName); i == argNames.end()) {	// 引数名チェック
							throw MmlException(MmlCompiler::ErrorCode::argumentUnknownError, next);
						}
						next = *r;
						flags.all = 0;
						flags.argValue = true;
						continue;
					}
				}
			}

			if (flags.argValue) {		// 引数値
				if (auto r = MmlCompiler::Util::parseWord(next)) {
					if (!r->word) {			// パースエラー
						throw MmlException(MmlCompiler::ErrorCode::argumentError, next);	// 文字列パースエラーとする
					}
					next = r->next;
					if (currentArgName.empty()) {
						result.argsList.emplace_back(std::move(*r->word));
						if (result.argsList.size() > argCount) {
							throw MmlException(MmlCompiler::ErrorCode::argumentError, next);	// 引数が多すぎる
						}
					} else {
						result.argsName[currentArgName] = std::move(*r->word);
						currentArgName.clear();
					}
					flags.all = 0;
					flags.rightParen = true;
					flags.comma = true;
					continue;
				}

				throw MmlException(MmlCompiler::ErrorCode::argumentError, next);
			}

			break;		// どれにも該当しないならエラー
		}
		throw MmlException(MmlCompiler::ErrorCode::functionCallError, next);
	};

	// 音長解析
	auto parseLength(const std::string_view& text, size_t defaultLength) {
		struct Result {
			std::string_view	next;	// 次の位置
			size_t	step = 0;
		};

		auto next = text;
		intmax_t step = 0;
		bool plus = true;
		while (true) {
			next = skipComment(next);		// コメントを読み飛ばす

			// ステップ数指定か?
			const bool stepNotation = [&] {
				const auto r = isStartsWith(next, '!');
				if (!r) return false;
				next = skipComment(*r);	// コメントを読み飛ばす
				return true;
			}();

			intmax_t tmpStep = defaultLength;

			// 数値
			if (const auto m = regexSearch(next, regex(R"(^([0-9]+))"))) {
				size_t n = boost::lexical_cast<size_t>((*m)[1].str());
				if (stepNotation) {
					tmpStep = n;
				} else {
					tmpStep = (MmlCompiler::timeBase * 4) / n;
				}
				next = skipComment({ (*m)[0].second, next.end() });
			} else {
				if (stepNotation) {		// ステップ数指定しておきながら数値がないなら
					throw MmlException(MmlCompiler::ErrorCode::lengthError, next);
				}
			}

			// 付点音符
			if (const auto m = regexSearch(next, regex(R"(^(\.)+)"))) {
				const size_t n = std::count((*m)[0].first, (*m)[0].second, '.');
				size_t t = tmpStep / 2;
				for (size_t j = 0; j < n; j++, t /= 2) {
					tmpStep += t;
				}
				next = skipComment({ (*m)[0].second, next.end() });		// コメントを読み飛ばす
			}

			step += tmpStep * (plus ? 1 : -1);

			// + -
			if (const auto r = isStartsWith(next, '-')) {
				next = *r;
				plus = false;
			} else if (const auto r = isStartsWith(next, '+')) {
				next = *r;
				plus = true;
			} else {
				break;
			}
		}

		if (step < 0) {		// 負値はエラー
			throw MmlException(MmlCompiler::ErrorCode::lengthMinusError, text);
		}
		Result result{ next,static_cast<decltype(Result::step)>(step) };
		return result;
	}


	namespace ParseFunc {
		struct Args {
			const std::string_view args;
			const std::string_view functionName;
			const bool disableArgName;

			template <typename F> std::string_view parse(F funcArg)const {
				std::string_view currentArgName;
				size_t argCount = 0;	// 名前ナシ引数数
				union {									// 次トークン情報
					struct {
						uint16_t	rightParen : 1;		// )
						uint16_t	comma : 1;			// ,
						uint16_t	argName : 1;		// 引数名
						uint16_t	argValue : 1;		// 引数値
					};
					uint16_t		all = 0;
				}flags;
				flags.rightParen = true;
				flags.argName = true;
				flags.argValue = true;

				auto next = args;
				while (true) {
					next = skipComment(next);		// コメントを読み飛ばす
					if (next.empty()) break;			// 先がないならエラー

					if (flags.rightParen) {
						if (const auto r = isStartsWith(next, ')')) {		// ")"
							return *r;	// 正常終了
						}
					}

					if (flags.comma) {
						if (const auto r = isStartsWith(next, ',')) {		// ","
							next = *r;
							flags.all = 0;
							flags.rightParen = true;
							flags.argName = true;
							flags.argValue = true;
							continue;
						}
					}

					if (flags.argName) {		// 引数名
						static const auto re = regex(R"(^([a-zA-Z]\w*))");
						if (const auto m = regexSearch(next, re)) {
							auto n = std::string_view((*m)[0].second, next.end());
							n = skipComment(n);			// コメントを読み飛ばす
							if (const auto r = isStartsWith(n, ':')) {		// ":" があれば引数名で確定
								currentArgName = std::string_view(std::to_address((*m)[0].first), (*m)[0].length());

								if (disableArgName) {	// 名前付き引数は非サポート
									throw MmlException(MmlCompiler::ErrorCode::argumentError, currentArgName);	// 名前アリ引数は未対応
								}

								next = *r;
								flags.all = 0;
								flags.argValue = true;
								continue;
							}
						}
					}

					if (flags.argValue) {		// 引数値
						const std::variant<std::string_view, size_t> argKey = currentArgName.empty() ? decltype(argKey)(argCount++) : decltype(argKey)(currentArgName);
						next = funcArg(argKey, next);
						flags.all = 0;
						flags.rightParen = true;
						flags.comma = true;
						currentArgName = std::string_view{};
						continue;
					}

					break;		// どれにも該当しないならエラー
				}
				throw MmlException(MmlCompiler::ErrorCode::functionCallError, next);

			}

		};

		std::optional<Args> parse(const std::string_view& text, const std::vector<std::string>& functionNames, bool disableArgName) {
			for (auto& name : functionNames) {
				const auto r = isStartsWith(text, name);
				if (!r) continue;
				auto next = skipComment(*r);		// コメントを読み飛ばす
				const auto r2 = isStartsWith(next, '(');	// 関数の"(" を確認
				if (!r2) continue;
				Args args{ *r2, { text.begin(), text.begin() + name.size() }, disableArgName };
				return std::optional(args);
			}
			return std::nullopt;
		};

	};

}

class MmlCompiler::Inner {
public:

	struct InterEvent : public MmlCompiler::EventBase {
		ParseNum::Assign 	assign;
	};
	struct InterPitchBend : public InterEvent {
		virtual std::shared_ptr<EventBase> clone()const {
			return std::make_shared<InterPitchBend>(*this);
		}
	};
	struct InterPan : public InterEvent {
		virtual std::shared_ptr<EventBase> clone()const {
			return std::make_shared<InterPan>(*this);
		}
	};
	struct InterExpression : public InterEvent {
		virtual std::shared_ptr<EventBase> clone()const {
			return std::make_shared<InterExpression>(*this);
		}
	};
	struct InterVolume : public InterEvent {
		virtual std::shared_ptr<EventBase> clone()const {
			return std::make_shared<InterVolume>(*this);
		}
	};
	struct InterFineTune : public InterEvent {
		virtual std::shared_ptr<EventBase> clone()const {
			return std::make_shared<InterFineTune>(*this);
		}
	};
	struct InterCoarseTune : public InterEvent {
		virtual std::shared_ptr<EventBase> clone()const {
			return std::make_shared<InterCoarseTune>(*this);
		}
	};
	struct InterMasterVolume : public InterEvent {
		virtual std::shared_ptr<EventBase> clone()const {
			return std::make_shared<InterMasterVolume>(*this);
		}
	};
	struct InterTempo : public InterEvent {
		virtual std::shared_ptr<EventBase> clone()const {
			return std::make_shared<InterTempo>(*this);
		}
	};

	struct Sequence {
		std::string			name;
		std::vector<Port>	ports;
		size_t				lastPosition = 0;
	};
	template <typename T> struct LessName {
		typedef void is_transparent;
		bool operator()(const T& a, const T& b)				const { return a->name < b->name; }
		bool operator()(const std::string_view& a, const T& b)	const { return a < b->name; }
		bool operator()(const T& a, const std::string_view& b)	const { return a->name < b; }
	};
	using Sequences = std::set<std::shared_ptr<const Sequence>, LessName<std::shared_ptr<const Sequence>>>;

	static std::vector<Port> mmlToSequence(const std::string_view& targetMml, const Sequences& sequences) {
		struct PortInfo {
			size_t						position = 0;			// 現在の位置
			size_t						defaultStep = 480;		// デフォルト音長(step)
			int							octave = 4;				// 現在のオクターブ( -2 ～ 8 )
			double						velocity = 100.0;		// 現在のベロシティ(0～127)
			std::shared_ptr<EventNote>	beforeEvent;			// 直前の音符( ^の対象)
			bool						noteUnmove = false;		// Noteで現在位置を進めないモード
			MmlCompiler::Port			port;
		};
		struct State {
			const Sequences& parentSequences;	// 親から(引数で)引き継がれたsequences
			std::vector<Result::Error> errors;
			std::map<std::string_view, PortInfo>	mapPort;	// <Port名,PortInfo>
			PortInfo* currentPort = nullptr;
			Sequences						sequences;
			struct PastedSequence {
				size_t							position = 0;
				std::shared_ptr<const Sequence>	sequence;
				std::optional<size_t>			length;
			};
			std::list<PastedSequence>	pastedSequences;
		}state = { sequences, };
		state.currentPort = &state.mapPort[""];

		struct Parser {
			ErrorCode errorCode;
			std::optional<std::string_view>(*func)(State&, const std::string_view&);
		};
		static const std::initializer_list<Parser> parsers = {

			// ^ tie (長さを付け足す)
			{ErrorCode::tieCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, '^');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);				// コメントを読み飛ばす
				auto& port = *state.currentPort;
				const auto len = parseLength(next, port.defaultStep);
				if (port.beforeEvent) {						// 直前の音符があれば
					port.beforeEvent->length += len.step;	// 音符の音長に足す
				}
				if (!port.noteUnmove || !port.beforeEvent) {
					port.position += len.step;
				}
				return len.next;
			}},

			// r?? 休符
			{ErrorCode::rCommandRangeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, 'r');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);				// コメントを読み飛ばす
				auto& port = *state.currentPort;
				const auto len = parseLength(next, port.defaultStep);
				port.position += len.step;
				port.beforeEvent.reset();				// '^'の対象をクリア
				return len.next;
			}},

			// CreatePort
			{ErrorCode::createPortError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = parseFunction(text, { "CreatePort","createPort" }, { "name","instrument","channel" });
				if (!r) return std::nullopt;
				const auto name = (*r).findArgString("name").value_or("");
				if (name.empty()) throw MmlException(ErrorCode::createPortPortNameError, text);
				auto r2 = state.mapPort.insert({ name ,PortInfo() });	// port追加
				if (!r2.second) throw MmlException(ErrorCode::createPortDuplicateError, text);	// 既にあるなら
				auto& port = r2.first->second;
				port.port.name = name;
				if (const auto s = r->findArgString("instrument")) port.port.instrument = *s;
				const auto ch = r->findArg<uintmax_t>("channel").second;
				if (!ch || *ch < 1 || *ch > 16) throw MmlException(ErrorCode::createPortChannelError, text);
				port.port.channel = static_cast<decltype(port.port.channel)>(*ch - 1);
				state.currentPort = &state.mapPort[name];
				return r->next;
			}},

			// a～g?? 音符
			{ErrorCode::noteCommandRangeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				static const regex re(R"(^([a-g][\+\-]?))");
				const auto m = regexSearch(text, re);
				if (!m) return std::nullopt;
				auto next = std::string_view((*m)[0].second, text.end());
				next = skipComment(next);			// コメントを読み飛ばす
				auto& port = *state.currentPort;
				const auto len = parseLength(next, port.defaultStep);
				auto e = std::make_shared<EventNote>();
				e->note = [&] {
					static const std::map<std::string, int> noteTable{
						{"c",	0},	{"c-",	-1},{"c+",	1},
						{"d",	2},	{"d-",	1},	{"d+",	3},
						{"e",	4},	{"e-",	3},	{"e+",	5},
						{"f",	5},	{"f-",	4},	{"f+",	6},
						{"g",	7},	{"g-",	6},	{"g+",	8},
						{"a",	9},	{"a-",	8},	{"a+",	10},
						{"b",	11},{"b-",	10},{"b+",	12},
					};
					if (auto i = noteTable.find((*m)[0].str()); i != noteTable.end()) {
						int note = (port.octave + 2) * 12 + i->second;
						if (note >= 0 && note <= 127) {		// 範囲チェック
							return note;
						}
					}
					throw MmlException(ErrorCode::noteCommandRangeError, text);
				}();
				e->length = len.step;
				e->velocity = std::clamp(static_cast<int>(std::lround(port.velocity)), 0, 127);
				port.port.eventList.emplace(port.position, e);
				if (!port.noteUnmove) {
					port.position += len.step;
				}
				port.beforeEvent = e;
				return len.next;
			}},

			// v? ベロシティ
			{ErrorCode::vCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, 'v');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);			// コメントを読み飛ばす
				const auto r2 = ParseNum::parse(next, true);
				if (!r2) throw MmlException(MmlCompiler::ErrorCode::vCommandError, text);
				const auto vel = r2->getValue();
				if (r2->ope == ParseNum::Ope::Set) {	// 絶対指定なら
					if (vel < 0 || vel > 127) throw MmlException(ErrorCode::vCommandRangeError, r2->matched);
				} else {								// 相対指定なら
					if (vel < -127 || vel > 127) throw MmlException(ErrorCode::vCommandRangeError, r2->matched);
				}
				auto& port = *state.currentPort;
				port.velocity = r2->apply(port.velocity);
				return r2->next;
			}},

			// < > オクターブUPDOWN
			{ ErrorCode::octaveUpDownCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto check = [&](int n) {
					auto& port = *state.currentPort;
					port.octave += n;
					if (port.octave < -2 || port.octave > 8) {	// 範囲チェック
						throw MmlException(ErrorCode::octaveUpDownRangeCommandError, text);
					}
				};
				if (const auto r = isStartsWith(text, '<')) {			// UP
					check(+1);
					return *r;
				} else if (const auto r = isStartsWith(text, '>')) {	// DOWN
					check(-1);
					return *r;
				}
				return std::nullopt;
			} },

			// o?? オクターブ
			{ ErrorCode::oCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, 'o');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);			// コメントを読み飛ばす
				static const regex re(R"(^(\-?[0-9]+))");
				const auto m = regexSearch(next, re);
				if (!m) throw MmlException(ErrorCode::oCommandRangeError, text);
				auto& port = *state.currentPort;
				int octave = boost::lexical_cast<int>((*m)[1].str());
				if (octave < -2 || octave > 8) {		// 範囲チェック
					throw MmlException(ErrorCode::oCommandRangeError, text);
				}
				port.octave = octave;
				port.beforeEvent.reset();				// '^'の対象をクリア
				return std::string_view((*m)[0].second, next.end());
			} },

			// l?? デフォルト音長
			{ ErrorCode::lCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, 'l');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);			// コメントを読み飛ばす
				auto& port = *state.currentPort;
				const auto len = parseLength(next, port.defaultStep);
				port.defaultStep = len.step;
				return len.next;
			} },

			// ' noteで位置更新するか否かモード
			{ ErrorCode::unknownError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, '\'');
				if (!r) return std::nullopt;
				auto& port = *state.currentPort;
				port.noteUnmove = !port.noteUnmove;		// 反転
				port.beforeEvent.reset();				// '^'の対象をクリア
				return *r;
			} },

			// @? プログラムチェンジ
			{ ErrorCode::programchangeCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, '@');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);			// コメントを読み飛ばす
				const auto m = regexSearch(next, regex(R"(^([0-9]+))"));
				if (!m) throw MmlException(ErrorCode::programchangeCommandError, next);
				auto& port = *state.currentPort;
				auto e = std::make_shared<EventProgramChange>();
				e->programNo = boost::lexical_cast<int>((*m)[1].str());
				port.port.eventList.emplace(port.position, e);
				return std::string_view((*m)[0].second, next.end());
			} },

			// t?? テンポ
			{ ErrorCode::tCommandRangeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, 't');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);			// コメントを読み飛ばす
				const auto r2 = ParseNum::parse(next, true);
				if (!r2) throw MmlException(MmlCompiler::ErrorCode::tCommandRangeError, text);
				const auto tempo = r2->getValue();
				if (r2->ope == ParseNum::Ope::Set) {	// 絶対指定なら
					if (tempo < 1 || tempo >= 1000) throw MmlException(ErrorCode::rangeError, r2->matched);
				} else {								// 相対指定なら
					if (tempo < -100 || tempo > 100) throw MmlException(ErrorCode::rangeError, r2->matched);
				}
				auto e = std::make_shared<InterTempo>();
				e->assign = *r2;
				auto& port = *state.currentPort;
				port.port.eventList.emplace(port.position, e);
				return r2->next;
			} },

			// Volume
			{ ErrorCode::volumeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "V","Volume" }, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = *state.currentPort;
						const auto r = ParseNum::parse(argValue, true);
						if (!r) throw MmlException(MmlCompiler::ErrorCode::volumeRangeError, argValue);
						if (r->ope == ParseNum::Ope::Set) {	// 絶対指定なら
							if (auto val = std::get_if<double>(&r->value)) {
								throw MmlException(ErrorCode::volumeRangeError, r->matched);	// 浮動小数はエラー
							} else if (auto val = std::get_if<intmax_t>(&r->value)) {
								if (*val < 0 || *val > 127) throw MmlException(ErrorCode::volumeRangeError, r->matched);
							} else assert(false);
						}
						auto e = std::make_shared<InterVolume>();
						e->assign = *r;
						port.port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			} },

			// Expression
			{ ErrorCode::expressionError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "Ep","Expression"}, true);
				if(!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = *state.currentPort;
						const auto r = ParseNum::parse(argValue, true);
						if (!r) throw MmlException(MmlCompiler::ErrorCode::expressionRangeError, argValue);
						if (r->ope == ParseNum::Ope::Set) {	// 絶対指定なら
							if (auto val = std::get_if<double>(&r->value)) {
								throw MmlException(ErrorCode::expressionRangeError, r->matched);	// 浮動小数はエラー
							} else if (auto val = std::get_if<intmax_t>(&r->value)) {
								if (*val < 0 || *val > 127) throw MmlException(ErrorCode::expressionRangeError, r->matched);
							} else assert(false);
						}
						auto e = std::make_shared<InterExpression>();
						e->assign = *r;
						port.port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			} },

			// ControlChange
			{ ErrorCode::controlChangeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "CC","ControlChange" }, { "no","value" });
				if (!r) return std::nullopt;
				const auto no = [&] {
					if (auto v = r->findArg<uintmax_t>(0).second) return *v;
					if (auto v = r->findArg<uintmax_t>("no").second) return *v;
					throw MmlException(ErrorCode::controlChangeError, text);
				}();
				const auto val = [&] {
					if (auto v = r->findArg<uintmax_t>(1).second) return *v;
					if (auto v = r->findArg<uintmax_t>("value").second) return *v;
					throw MmlException(ErrorCode::controlChangeError, text);
				}();
				if (no < 0 || no > 127 || val < 0 || val > 127) {
					throw MmlException(ErrorCode::controlChangeRangeError, text);
				}
				auto& port = *state.currentPort;
				auto e = std::make_shared<EventControlChange>();
				e->no = static_cast<decltype(e->no)>(no);
				e->value = static_cast<decltype(e->value)>(val);
				port.port.eventList.emplace(port.position, e);
				return r->next;
			} },
				
			// PitchBend
			{ ErrorCode::pitchBendError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "PitchBend" }, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = *state.currentPort;
						const auto r = ParseNum::parse(argValue);
						if (!r) throw MmlException(MmlCompiler::ErrorCode::pitchBendRangeError, argValue);
						if (r->ope == ParseNum::Ope::Set) {	// 絶対指定なら
							if (auto val = std::get_if<double>(&r->value)) {
								throw MmlException(ErrorCode::pitchBendRangeError, r->matched);	// 浮動小数はエラー
							} else if (auto val = std::get_if<intmax_t>(&r->value)) {
								if (*val < -8192 || *val > 8191) throw MmlException(ErrorCode::pitchBendRangeError, r->matched);
							} else assert(false);
						}
						auto e = std::make_shared<InterPitchBend>();
						e->assign = *r;
						port.port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			} },

			// Pan
			{ ErrorCode::panError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "Pan"}, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = *state.currentPort;
						const auto r = ParseNum::parse(argValue, true);
						if (!r) throw MmlException(MmlCompiler::ErrorCode::panRangeError, argValue);
						if (r->ope == ParseNum::Ope::Set) {	// 絶対指定なら
							if (auto val = std::get_if<double>(&r->value)) {
								throw MmlException(ErrorCode::panRangeError, r->matched);	// 浮動小数はエラー
							} else if (auto val = std::get_if<intmax_t>(&r->value)) {
								if (*val < 0 || *val > 127) throw MmlException(ErrorCode::panRangeError, r->matched);
							} else assert(false);
						}
						auto e = std::make_shared<InterPan>();
						e->assign = *r;
						port.port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			} },

			// Port
			{ ErrorCode::portError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "Port","port" }, {});
				if (!r) return std::nullopt;
				const auto name = (*r).findArgString(0).value_or("");
				if (name.empty()) {
					throw MmlException(ErrorCode::portNameError, text);
				}
				if (const auto i = state.mapPort.find(name); i == state.mapPort.end()) {	// 存在しないportならエラー
					throw MmlException(ErrorCode::portNameError, text);
				}
				state.currentPort = &state.mapPort[name];
				return r->next;
			} },
				
			// CreateSequence
			{ErrorCode::createSequenceError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "CreateSeq","CreateSequence" }, { "name","mml" });
				if (!r) return std::nullopt;
				const auto name = (*r).findArgString("name").value_or("");
				if (name.empty()) {
					throw MmlException(ErrorCode::createSequenceNameError, text);
				}
				if (const auto i = state.sequences.find(name); i != state.sequences.end()) {	// 既にあるなら
					throw MmlException(ErrorCode::createSequenceDuplicateError, text);
				}
				const auto mml = (*r).findArg<std::string_view>("mml").second;
				if (!mml) {
					throw MmlException(ErrorCode::createSequenceError, text);
				}
				Sequence seq;
				seq.name = name;

				try {
					Sequences parentSeq = state.parentSequences;
					for (auto s : state.sequences) parentSeq.insert(s);		// sequencesを合成
					seq.ports = mmlToSequence(*mml, parentSeq);
				} catch (MmlException& e) {
					state.errors.insert(state.errors.end(), e.errors.begin(), e.errors.end());
				}

				seq.lastPosition = [&] {
					size_t p = 0;
					for (auto& port : seq.ports) {
						if (auto i = port.eventList.rbegin(); i != port.eventList.rend()) {
							p = (std::max)(p, i->first);
						}
					}
					return p;
				}();
				auto spSequence = std::make_shared<Sequence>(std::move(seq));
				state.sequences.insert(spSequence);
				return r->next;
			}},

			// Sequence
			{ErrorCode::sequenceError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "Seq","Sequence" }, { "length" });
				if (!r) return std::nullopt;
				const auto name = (*r).findArgString(0).value_or("");
				if (name.empty()) throw MmlException(ErrorCode::sequenceNameError, text);
				decltype(state.pastedSequences)::value_type seq;
				seq.sequence = [&] {
					if (auto i = state.sequences.find(name); i != state.sequences.end()) {
						return *i;
					}
					if (auto i = state.parentSequences.find(name); i != state.parentSequences.end()) {
						return *i;
					}
					throw MmlException(ErrorCode::sequenceNameError, text);
				}();
				auto& port = *state.currentPort;
				seq.position = port.position;
				seq.length = [&]()->std::optional<size_t> {
					const auto arg = (*r).findArg<std::string_view>("length");
					if (!arg.first) return std::nullopt;	// 指定ナシ
					if (!arg.second) throw MmlException(ErrorCode::sequenceLengthError, text);	// 型が違う
					const auto& lengthText = *arg.second;
					const auto len = parseLength(lengthText, port.defaultStep);
					if ((len.next.data() - lengthText.data()) != lengthText.size()) {
						throw MmlException(ErrorCode::sequenceLengthError, text);		// 余計な文字列がある
					}
					return len.step;
				}();
				if (!port.noteUnmove) {
					if (seq.length) {
						port.position += *seq.length;
					} else {
						const auto step = seq.sequence->lastPosition;
						const auto remainder = step % 1920;
						const auto skip = (remainder == 0) ? step + 1920 : step + (1920 - remainder);
						port.position += skip;
					}
				}
				state.pastedSequences.emplace_back(std::move(seq));
				return r->next;
			}},

			// FineTune
			{ErrorCode::fineTuneError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "FineTune"}, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = *state.currentPort;
						const auto r = ParseNum::parse(argValue);
						if (!r) throw MmlException(MmlCompiler::ErrorCode::fineTuneRangeError, argValue);
						const double n = r->getValue();
						if (r->ope == ParseNum::Ope::Set) {	// 絶対指定なら
							if (n < -100.0 || n > 100.0) throw MmlException(ErrorCode::fineTuneRangeError, r->matched);
						} else {							// 相対指定なら
							if (n < -200.0 || n > 200.0) throw MmlException(ErrorCode::fineTuneRangeError, r->matched);
						}
						auto e = std::make_shared<InterFineTune>();
						e->assign = *r;
						port.port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			}},

			// CoarseTune
			{ErrorCode::coarseTuneError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "CoarseTune"}, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = *state.currentPort;
						const auto r = ParseNum::parse(argValue);
						if (!r) throw MmlException(MmlCompiler::ErrorCode::coarseTuneRangeError, argValue);
						if (r->ope == ParseNum::Ope::Set) {	// 絶対指定なら
							if (std::get_if<double>(&r->value)) throw MmlException(ErrorCode::panRangeError, r->matched);	// 浮動小数はエラー
						}
						const double n = r->getValue();
						if (n < -64 || n > 63) throw MmlException(ErrorCode::coarseTuneRangeError, r->matched);
						auto e = std::make_shared<InterCoarseTune>();
						e->assign = *r;
						port.port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			} },

			// MasterVolume
			{ ErrorCode::masterVolumeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "MasterVolume"}, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = *state.currentPort;
						const auto r = ParseNum::parse(argValue, true);
						if (!r) throw MmlException(MmlCompiler::ErrorCode::masterVolumeRangeError, argValue);
						if (r->ope == ParseNum::Ope::Set) {	// 絶対指定なら
							if (auto val = std::get_if<double>(&r->value)) {
								throw MmlException(ErrorCode::masterVolumeRangeError, r->matched);	// 浮動小数はエラー
							} else if (auto val = std::get_if<intmax_t>(&r->value)) {
								if (*val < 0 || *val > 16383) throw MmlException(ErrorCode::masterVolumeRangeError, r->matched);
							} else assert(false);
						}
						auto e = std::make_shared<InterMasterVolume>();
						e->assign = *r;
						port.port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			} },

			// Meta
			{ErrorCode::metaError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "Meta" }, { "type" });
				if (!r) return std::nullopt;
				const auto type = (*r).findArgInt("type");
				if (!type || *type < 0 || *type > std::numeric_limits<uint8_t>::max()) {
					throw MmlException(ErrorCode::metaTypeError, text);
				}
				auto& port = *state.currentPort;
				auto e = std::make_shared<EventMeta>();
				e->type = static_cast<decltype(e->type)>(*type);
				for (auto& arg : r->argsList) {
					std::visit([&](auto&& val) {
						using T = std::decay_t<decltype(val)>;
						if constexpr (std::is_same_v<T, uintmax_t>) {
							if (val > std::numeric_limits<uint8_t>::max()) throw MmlException(ErrorCode::metaTypeError, text);
							e->data.push_back(static_cast<uint8_t>(val));
						} else if constexpr (std::is_same_v<T, intmax_t>) {
							if (val < std::numeric_limits<uint8_t>::min() || val > std::numeric_limits<uint8_t>::max()) throw MmlException(ErrorCode::metaTypeError, text);
							e->data.push_back(static_cast<uint8_t>(val));
						} else if constexpr (std::is_same_v<T, std::string_view>) {
							e->data.insert(e->data.end(), val.begin(), val.end());
						} else {
							throw MmlException(ErrorCode::metaTypeError, text);
						}
					}, arg);
				}
				port.port.eventList.emplace(port.position, e);
				return r->next;
			}},

			// SysEx システムエクスクルーシブ
			{ErrorCode::sysExError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "SysEx" }, {});
				if (!r) return std::nullopt;
				auto& port = *state.currentPort;
				auto e = std::make_shared<EventSystemExclusive>();
				for (auto& arg : r->argsList) {
					std::visit([&](auto&& val) {
						using T = std::decay_t<decltype(val)>;
						if constexpr (std::is_same_v<T, uintmax_t>) {
							if (val > 0xff) throw MmlException(ErrorCode::sysExArgError, text);
							e->data.push_back(static_cast<uint8_t>(val));
						} else if constexpr (std::is_same_v<T, intmax_t>) {
							if (val < 0 || val > 0xff) throw MmlException(ErrorCode::sysExArgError, text);
							e->data.push_back(static_cast<uint8_t>(val));
						} else if constexpr (std::is_same_v<T, std::string_view>) {
							e->data.insert(e->data.end(), val.begin(), val.end());
						} else {
							throw MmlException(ErrorCode::sysExArgError, text);
						}
					}, arg);
				}
				if (e->data.size() <= 0 || (e->data[0] != 0xf7 && e->data[0] != 0xf0)) {	// 先頭バイトチェック
					throw MmlException(ErrorCode::sysExArgFirstError, text);
				}
				port.port.eventList.emplace(port.position, e);
				return r->next;
			}},

			// DefinePresetFM FM音色定義(rlib-MML 固有メタイベント)
			{ErrorCode::definePresetFMError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				static constexpr std::array maxTable = {
					//  AR  DR  SR  RR  SL  TL KS  ML DT
						31, 31, 31, 15, 15,127, 3, 15, 7,
						31, 31, 31, 15, 15,127, 3, 15, 7,
						31, 31, 31, 15, 15,127, 3, 15, 7,
						31, 31, 31, 15, 15,127, 3, 15, 7,
						7,	7,	//  AL  FB
				};
				auto r = parseFunction(text, { "DefinePresetFM" }, { "no","name" }, maxTable.size());
				if (!r) return std::nullopt;
				const auto no = (*r).findArgInt("no");
				if (!no || *no < 0 || *no>127) {
					throw MmlException(ErrorCode::definePresetFMNoError, text);
				}
				const auto name = (*r).findArgString("name").value_or("");
				Json::Array parameter;
				for (size_t i = 0; i < maxTable.size(); i++) {
					const auto n = (*r).findArgInt(i);
					if (!n) throw MmlException(ErrorCode::definePresetFMError, text);
					if (*n<0 || *n> maxTable[i]) throw MmlException(ErrorCode::definePresetFMRangeError, text);
					parameter.push_back(*n);
				}
				const Json j = Json::Map{
					{"rlib-MML", Json::Map{
						{"opnfm", Json::Map{
							{std::to_string(*no), Json::Map{
								{"name", name},
								{"reg", parameter},
							}},
						}},
					}},
				};
				auto& port = *state.currentPort;
				auto e = std::make_shared<EventMeta>();
				e->type = static_cast<decltype(e->type)>(midi::EventMeta::Type::sequencerLocal);
				auto stringified = j.stringify();
				e->data = { stringified.begin(), stringified.end() };
				auto it = port.port.eventList.emplace(port.position, e);
				return r->next;
			}},

			// DefinePresetPSG PSG(SSG)音色定義(rlib-MML 固有メタイベント)
			{ErrorCode::definePresetPSGError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				// AR,HR,DR,RR	アタック/ホールド/ディケイ/リリース時間(秒、0～60)
				// SL			サステインレベル(0.0～1.0)
				// noise		ノイズ周波数(-1:OFF, 0～31)
				// tone			トーン ON(1)/OFF(0)
				constexpr double timeMax = 60.0;
				static constexpr std::array<std::pair<double, double>, 5> envRange = { {
					{0.0,timeMax}, {0.0,timeMax}, {0.0,timeMax}, {0.0,1.0}, {0.0,timeMax},
				} };	// AR,HR,DR,SL,RR の順

				auto r = parseFunction(text, { "DefinePresetPSG" }, { "no","name" }, envRange.size() + 2);	// +2: noise,tone
				if (!r) return std::nullopt;
				const auto no = (*r).findArgInt("no");
				if (!no || *no < 0 || *no>127) {
					throw MmlException(ErrorCode::definePresetPSGNoError, text);
				}
				const auto name = (*r).findArgString("name").value_or("");

				Json::Array parameter;
				for (size_t i = 0; i < envRange.size(); i++) {
					const auto n = (*r).findArgNumber(i);
					if (!n) throw MmlException(ErrorCode::definePresetPSGError, text);
					if (*n < envRange[i].first || *n > envRange[i].second) throw MmlException(ErrorCode::definePresetPSGRangeError, text);
					parameter.push_back(*n);
				}
				{// noise (-1～31)
					const auto n = (*r).findArgInt(envRange.size());
					if (!n) throw MmlException(ErrorCode::definePresetPSGError, text);
					if (*n < -1 || *n > 31) throw MmlException(ErrorCode::definePresetPSGRangeError, text);
					parameter.push_back(*n);
				}
				{// tone (0 or 1)
					const auto n = (*r).findArgInt(envRange.size() + 1);
					if (!n) throw MmlException(ErrorCode::definePresetPSGError, text);
					if (*n < 0 || *n > 1) throw MmlException(ErrorCode::definePresetPSGRangeError, text);
					parameter.push_back(*n);
				}

				const Json j = Json::Map{
					{"rlib-MML", Json::Map{
						{"opnpsg", Json::Map{
							{std::to_string(*no), Json::Map{
								{"name", name},
								{"reg", parameter},
							}},
						}},
					}},
				};
				auto& port = *state.currentPort;
				auto e = std::make_shared<EventMeta>();
				e->type = static_cast<decltype(e->type)>(midi::EventMeta::Type::sequencerLocal);
				auto stringified = j.stringify();
				e->data = { stringified.begin(), stringified.end() };
				auto it = port.port.eventList.emplace(port.position, e);
				return r->next;
			}},
		};

		for (auto next = targetMml; true;) {
			try {
				next = skipComment(next);			// コメントを読み飛ばす
				if (next.empty()) break;					// 完了
				auto i = parsers.begin();
				for (; i != parsers.end(); i++) {
					try {
						if (const auto r = i->func(state, next)) {
							next = *r;
							break;
						}
					} catch (const MmlException& e) {
						throw e;
					} catch (...) {
						throw MmlException(i->errorCode, next);
					}
				}
				if (i == parsers.end()) throw MmlException(ErrorCode::unknownError, next);	// 未定義文字列エラー
			} catch (MmlException& e) {
				state.errors.insert(state.errors.end(), e.errors.begin(), e.errors.end());

				// 行末まで飛ぶ(改行が無ければ末尾まで飛ぶ)
				if (auto pos = next.find_first_of("\r\n"); pos != std::string_view::npos) {
					next = next.substr(pos);
				} else {
					next = {};
				}

			}
		}
		if (state.errors.size() > 0) throw MmlException(std::move(state.errors));

		std::vector<Port> ports; // Result result;
		for (auto& i : state.mapPort) {
			ports.emplace_back(std::move(i.second.port));
		}

		// Sequence 展開
		for (auto& i : state.pastedSequences) {
			size_t postion = i.position;
			auto& spSequence = i.sequence;
			const size_t length = i.length ? *i.length : (std::numeric_limits<size_t>::max)();

			for (auto& port : spSequence->ports) {
				Port p;
				p.name = port.name;
				p.instrument = port.instrument;
				p.channel = port.channel;

				for (auto& event : port.eventList) {
					auto sp = event.second->clone();
					if (event.first >= length) break;			// length より後は採用しない
					p.eventList.emplace(event.first + postion, sp);
				}

				ports.emplace_back(std::move(p));
			}
		}

		return ports;
	}

};

MmlCompiler::Result MmlCompiler::compile(const std::shared_ptr<const std::string>& mml) {
	Result r{ mml };
	try {
		r.ports = Inner::mmlToSequence(*mml, MmlCompiler::Inner::Sequences());

		// 中間イベントのパース
		struct Channel {
			double expression = 127.0;	// エクスプレッション (0.0～127.0)
			double volume = 100.0;		// ボリューム (0.0～127.0)
			double pan = 64;			// パン (0.0～127.0)
			double pitchBend = 0.0;		// ピッチベンド (-8192～8191)
			double fineTune = 0.0;		// FineTune (-100.0～100.0)
			double coarseTune = 0.0;	// coarseTune (-64.0～63.0)
		};
		struct Instrument {
			double masterVolume = 16383.0;	// マスターボリューム
			std::map<uint8_t, Channel>	channels;	// <channelNo,Channel>
		};
		struct State {
			std::map<std::string_view, Instrument> instruments;		// <instrument名,Instrument>
			double tempo = 120.0;			// テンポ
		}state;

		const auto events = [&] {
			struct EventInfo {
				State& state;
				std::map<std::string_view, Instrument>::iterator itInstrument;
				std::map<uint8_t, Channel>::iterator itChannel;
				std::multimap<size_t, std::shared_ptr<const EventBase>>& eventList;
				std::multimap<size_t, std::shared_ptr<const EventBase>>::iterator itEvent;
			};
			std::multimap<size_t, EventInfo> events;
			for (auto& port : r.ports) {
				const auto [itInstrument, isInsertedInstrument] = state.instruments.try_emplace(port.instrument);
				const auto [itChannel, isInsertedChannel] = itInstrument->second.channels.try_emplace(port.channel);
				for (auto itEvent = port.eventList.begin(); itEvent != port.eventList.end(); itEvent++) {
					if (std::dynamic_pointer_cast<const Inner::InterEvent>(itEvent->second)) {
						events.emplace(itEvent->first, EventInfo{ state, itInstrument, itChannel, port.eventList, itEvent });
					}
				}
			}
			return events;
		}();

		for (auto& event : events) {

			static const std::map<std::type_index, void (*)(const decltype(events)::value_type&)> map = {
				{typeid(Inner::InterPitchBend), [](const decltype(events)::value_type& event) {
					auto& e = static_cast<const Inner::InterPitchBend&>(*event.second.itEvent->second);
					Channel& ch = event.second.itChannel->second;
					ch.pitchBend = e.assign.apply(ch.pitchBend);
					auto ev = std::make_shared<EventPitchBend>();
					ev->pitchBend = std::clamp(static_cast<int>(std::lround(ch.pitchBend)), -8192, 8191);
					event.second.itEvent->second = ev;		// Eventを置換
				}},
				{typeid(Inner::InterPan), [](const decltype(events)::value_type& event) {
					auto& e = static_cast<const Inner::InterPan&>(*event.second.itEvent->second);
					Channel& ch = event.second.itChannel->second;
					ch.pan = e.assign.apply(ch.pan);
					auto ev = std::make_shared<EventControlChange>();
					ev->no = static_cast<decltype(ev->no)>(midi::EventControlChange::Type::pan);
					ev->value = std::clamp(static_cast<int>(std::lround(ch.pan)), 0, 127);
					event.second.itEvent->second = ev;		// Eventを置換
				}},
				{typeid(Inner::InterExpression), [](const decltype(events)::value_type& event) {
					auto& e = static_cast<const Inner::InterExpression&>(*event.second.itEvent->second);
					Channel& ch = event.second.itChannel->second;
					ch.expression = e.assign.apply(ch.expression);
					auto ev = std::make_shared<EventControlChange>();
					ev->no = static_cast<decltype(ev->no)>(midi::EventControlChange::Type::expression);
					ev->value = std::clamp(static_cast<int>(std::lround(ch.expression)), 0, 127);
					event.second.itEvent->second = ev;		// Eventを置換
				}},
				{typeid(Inner::InterVolume), [](const decltype(events)::value_type& event) {
					auto& e = static_cast<const Inner::InterVolume&>(*event.second.itEvent->second);
					Channel& ch = event.second.itChannel->second;
					ch.volume = e.assign.apply(ch.volume);
					auto ev = std::make_shared<EventControlChange>();
					ev->no = static_cast<decltype(ev->no)>(midi::EventControlChange::Type::volume);
					ev->value = std::clamp(static_cast<int>(std::lround(ch.volume)), 0, 127);
					event.second.itEvent->second = ev;		// Eventを置換
				}},
				{typeid(Inner::InterFineTune), [](const decltype(events)::value_type& event) {
					auto& e = static_cast<const Inner::InterFineTune&>(*event.second.itEvent->second);
					Channel& ch = event.second.itChannel->second;
					ch.fineTune = e.assign.apply(ch.fineTune);
					const auto toRaw = [](double fineTune) {
						constexpr double inMin = -100.0, inMax = 100.0;
						constexpr int outMax = 16383;
						return std::clamp(static_cast<int>(std::round((fineTune - inMin) * (outMax + 1) / (inMax - inMin))), 0, outMax);	// -8192～0～8192 スケール(+100には微妙に届かない)
					};
					const auto raw = toRaw(ch.fineTune);

					struct {
						midi::EventControlChange::Type no;
						uint8_t val;
					}const tbl[] = {
						{midi::EventControlChange::Type::rpnMSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::fineTune) / 0x80 & 0x7f	},
						{midi::EventControlChange::Type::rpnLSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::fineTune) & 0x7f	},
						{midi::EventControlChange::Type::dataEntryMSB,	static_cast<uint8_t>(raw / 0x80 & 0x7f)	},
						{midi::EventControlChange::Type::dataEntryLSB,	static_cast<uint8_t>(raw & 0x7f)	},
					};
					for (auto& t : tbl) {
						auto e = std::make_shared<EventControlChange>();
						e->no = static_cast<decltype(e->no)>(t.no);
						e->value = t.val;
						event.second.eventList.emplace_hint(event.second.itEvent, event.second.itEvent->first, e);	// Eventを挿入
					}
					event.second.eventList.erase(event.second.itEvent);	// 不要になったEventを削除
				}},
				{typeid(Inner::InterCoarseTune), [](const decltype(events)::value_type& event) {
					auto& e = static_cast<const Inner::InterCoarseTune&>(*event.second.itEvent->second);
					Channel& ch = event.second.itChannel->second;
					ch.coarseTune = e.assign.apply(ch.coarseTune);
					const int val = std::clamp(static_cast<int>(std::lround(ch.coarseTune)), -64, 63);
					struct {
						midi::EventControlChange::Type no;
						uint8_t val;
					}const tbl[] = {
							{midi::EventControlChange::Type::rpnMSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::coarseTune) / 0x80 & 0x7f	},
							{midi::EventControlChange::Type::rpnLSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::coarseTune) & 0x7f	},
							{midi::EventControlChange::Type::dataEntryMSB,	static_cast<uint8_t>((val + 64) & 0x7f)	},
							{midi::EventControlChange::Type::dataEntryLSB,	0	},
					};
					for (auto& t : tbl) {
						auto e = std::make_shared<EventControlChange>();
						e->no = static_cast<decltype(e->no)>(t.no);
						e->value = t.val;
						event.second.eventList.emplace_hint(event.second.itEvent, event.second.itEvent->first, e);	// Eventを挿入
					}
					event.second.eventList.erase(event.second.itEvent);	// 不要になったEventを削除
				}},
				{typeid(Inner::InterMasterVolume), [](const decltype(events)::value_type& event) {
					auto& e = static_cast<const Inner::InterMasterVolume&>(*event.second.itEvent->second);
					Instrument& inst = event.second.itInstrument->second;
					inst.masterVolume = e.assign.apply(inst.masterVolume);
					auto ev = std::make_shared<EventSystemExclusive>();
					midi::utility::Bit14 u(static_cast<uint16_t>(std::clamp(static_cast<int>(std::lround(inst.masterVolume)), 0, 16383)));
					ev->data = { 0xf0, 0x7f, 0x7f, 0x04, 0x1, static_cast<uint8_t>(u.lsb), static_cast<uint8_t>(u.msb), 0xf7 };
					event.second.itEvent->second = ev;		// Eventを置換
				}},
				{typeid(Inner::InterTempo), [](const decltype(events)::value_type& event) {
					auto& e = static_cast<const Inner::InterTempo&>(*event.second.itEvent->second);
					event.second.state.tempo = e.assign.apply(event.second.state.tempo);
					assert(std::isfinite(event.second.state.tempo));
					auto ev = std::make_shared<EventMeta>();
					auto t = midi::EventMeta::createTempo(std::clamp(event.second.state.tempo, 1.0, 1000.0));	// createTempoを利用
					ev->type = static_cast<decltype(ev->type)>(t.type);
					ev->data = t.data;
					event.second.itEvent->second = ev;		// Eventを置換
				}},

			};

			if (const auto i = map.find(typeid(*event.second.itEvent->second)); i != map.end()) {
				(i->second)(event);
			}

		}

	} catch (MmlException& e) {
		r.errors = e.errors;
	}
	return r;
}

std::optional<MmlCompiler::Util::ParsedWord> MmlCompiler::Util::parseWord(const std::string_view& text) {

	ParsedWord parsedWord;

	// 文字列リテラルパース
	if (auto r = parseString(text)) {
		parsedWord.next = r->next;
		if (r->value) {		// パースエラーチェック
			parsedWord.word = *r->value;
		}
		return parsedWord;
	}

	// 整数パース
	if (auto r = parseInt(text)) {
		if (!r->next.starts_with('.')) {	// 直後に . がないことを確認。あれば浮動小数点数として次へ
			parsedWord.next = r->next;
			if (auto p = std::get_if<intmax_t>(&r->value)) {
				parsedWord.word = *p;
			} else if (auto p = std::get_if<uintmax_t>(&r->value)) {
				parsedWord.word = *p;
			} else assert(false);
			return parsedWord;
		}
	}

	// 浮動小数点数パース
	if (auto r = parseDouble(text)) {
		parsedWord.next = r->next;
		parsedWord.word = r->value;
		return parsedWord;
	}

	{// みなし文字列
		static const auto re = regex(R"(^([a-zA-Z_][\w\+\-]*))");		// 頭文字は英字_ で英字数値_+- が対象
		if (const auto m = regexSearch(text, re)) {
			parsedWord.next = std::string_view((*m)[0].second, text.end());
			parsedWord.word = std::string_view((*m)[0].first, (*m)[0].second);
			return parsedWord;
		}
	}

	return std::nullopt;
}


void MmlCompiler::unitTest() {


	{// parseInt
		std::cout << "parseInt" << std::endl;
		struct {
			std::string text;
			std::pair<size_t, std::variant<intmax_t, uintmax_t>> result;
		}static const tbl[] = {
			{"",			{0,static_cast<uintmax_t>(0)}},
			{"+",			{0,static_cast<uintmax_t>(0)}},
			{"-",			{0,static_cast<uintmax_t>(0)}},
			{"0",			{1,static_cast<uintmax_t>(0)}},
			{"-0.",			{2,static_cast<intmax_t>(0)}},
			{"1.2",			{1,static_cast<uintmax_t>(1)}},
			{"2e1.2",		{1,static_cast<uintmax_t>(2)}},
			{"+3e1.2",		{2,static_cast<intmax_t>(3)}},
			{"-4e1.2",		{2,static_cast<intmax_t>(-4)}},
			{"0x3a1.e1.2",	{5,static_cast<uintmax_t>(0x3a1)}},
			{"+0x3e1.2",	{2,static_cast<intmax_t>(0)}},
			{"0x",			{0,static_cast<intmax_t>(0)}},
			{"+x",			{0,static_cast<intmax_t>(0)}},
			{"++0",			{0,static_cast<intmax_t>(0)}},
			{"--1",			{0,static_cast<intmax_t>(0)}},
			{"+-1",			{0,static_cast<intmax_t>(0)}},
			{"-.",			{0,static_cast<intmax_t>(0)}},
		};
		for (auto& t : tbl) {
			std::cout << t.text << std::endl;
			auto r = parseInt(t.text);
			if (t.result.first == 0) {
				assert(!r);
			} else {
				assert(r->value == t.result.second);
				assert(t.result.first == r->next.data() - t.text.data());
			}
		}
	}

	{// parseNum
		std::cout << "parseNum" << std::endl;
		using Ope = ParseNum::Ope;
		struct {
			std::string text;
			std::tuple<size_t, Ope, std::variant<intmax_t, double>> res, resOffset;
		}static const tbl[] = {
			{"1.5",			{3, Ope::Set, 1.5},		{3, Ope::Set, 1.5},		},

			{"",			{0, Ope::Set, 0},		{0, Ope::Set, 0},		},
			{"0",			{1, Ope::Set, 0},		{1, Ope::Set, 0},		},
			{"0a",			{1, Ope::Set, 0},		{1, Ope::Set, 0},		},
			{"1",			{1, Ope::Set, 1},		{1, Ope::Set, 1},		},
			{"+1",			{2, Ope::Set, 1},		{2, Ope::Add, 1},		},
			{"-1",			{2, Ope::Set, -1},		{2, Ope::Sub, 1},		},
			{"1.5",			{3, Ope::Set, 1.5},		{3, Ope::Set, 1.5},		},
			{"1.5e4",		{3, Ope::Set, 1.5},		{3, Ope::Set, 1.5},		},	// 指数表記は非サポート
			{"1.5e-2",		{3, Ope::Set, 1.5},		{3, Ope::Set, 1.5},		},	// 指数表記は非サポート
			{"1.5e5a",		{3, Ope::Set, 1.5},		{3, Ope::Set, 1.5},		},	// 指数表記は非サポート
			{"+1.5",		{4, Ope::Set, 1.5},		{4, Ope::Add, 1.5},		},
			{"-1.5",		{4, Ope::Set, -1.5},	{4, Ope::Sub, 1.5},		},
			{"0x10",		{4, Ope::Set, 0x10},	{4, Ope::Set, 0x10},	},
			{"+0x10",		{5, Ope::Set, 0x10},	{5, Ope::Add, 0x10},	},
			{"-0x10",		{5, Ope::Set, 0 - 0x10},{5, Ope::Sub, 0x10},	},
			{"0x1a",		{4, Ope::Set, 0x1a},	{4, Ope::Set, 0x1a},	},
			{"0x1ag",		{4, Ope::Set, 0x1a},	{4, Ope::Set, 0x1a},	},
			{"0xg",			{0, Ope::Set, 0},		{0, Ope::Set, 0},		},	// 注意: "0x"の次に16進数がない場合はNGとする。("0"まででOKとはしない)
			{"+0xg",		{0, Ope::Set, 0},		{0, Ope::Set, 0},		},	// 注意: "0x"の次に16進数がない場合はNGとする。("+0"まででOKとはしない)

			{"+=",			{0, Ope::Set, 0},		{0, Ope::Set, 0},		},
			{"+=0",			{3, Ope::Add, 0},		{3, Ope::Add, 0},		},
			{"+=0a",		{3, Ope::Add, 0},		{3, Ope::Add, 0},		},
			{"+=1",			{3, Ope::Add, 1},		{3, Ope::Add, 1},		},
			{"++=1",		{0, Ope::Set, 0},		{0, Ope::Set, 0},		},
			{"+-=1",		{0, Ope::Set, 0},		{0, Ope::Set, 0},		},
			{"+=+1",		{4, Ope::Add, 1},		{4, Ope::Add, 1},		},
			{"+=-1",		{4, Ope::Add, -1},		{4, Ope::Add, -1},		},
			{"+=1.5",		{5, Ope::Add, 1.5},		{5, Ope::Add, 1.5},		},
			{"+=1.5e-2",	{5, Ope::Add, 1.5},		{5, Ope::Add, 1.5},		},	// 指数表記は非サポート
			{"+=1.5e4",		{5, Ope::Add, 1.5},		{5, Ope::Add, 1.5},		},	// 指数表記は非サポート
			{"+=-1.5e4",	{6, Ope::Add, -1.5},	{6, Ope::Add, -1.5},	},	// 指数表記は非サポート
			{"+=+1.5",		{6, Ope::Add, 1.5},		{6, Ope::Add, 1.5},		},
			{"+=-1.5",		{6, Ope::Add, -1.5},	{6, Ope::Add, -1.5},	},
			{"+=0x10",		{6, Ope::Add, 0x10},	{6, Ope::Add, 0x10},	},
			{"+=+0x10",		{7, Ope::Add, 0x10},	{7, Ope::Add, 0x10},	},
			{"+=-0x10",		{7, Ope::Add, 0 - 0x10},{7, Ope::Add, 0 - 0x10},},
			{"+=-0xg",		{0, Ope::Set, 0},		{0, Ope::Set, 0},		},
			{"+=0x1a",		{6, Ope::Add, 0x1a},	{6, Ope::Add, 0x1a},	},
			{"+=0x1ag",		{6, Ope::Add, 0x1a},	{6, Ope::Add, 0x1a},	},

			{"-=12c",		{4, Ope::Sub, 12},		{4, Ope::Sub, 12},		},
			{"-=1.5d",		{5, Ope::Sub, 1.5},		{5, Ope::Sub, 1.5},		},
			{"-=1.5e4a",	{5, Ope::Sub, 1.5},		{5, Ope::Sub, 1.5},		},	// 指数表記は非サポート
			{"-=0x123ag",	{8, Ope::Sub, 0x123a},	{8, Ope::Sub, 0x123a},	},

			{"/=12c",		{4, Ope::Div, 12},		{4, Ope::Div, 12},		},
			{"/=1.5d",		{5, Ope::Div, 1.5},		{5, Ope::Div, 1.5},		},
			{"/=1.5e4a",	{5, Ope::Div, 1.5},		{5, Ope::Div, 1.5},		},	// 指数表記は非サポート
			{"/=0x123ag",	{8, Ope::Div, 0x123a},	{8, Ope::Div, 0x123a},	},

			{"*=12c",		{4, Ope::Mul, 12},		{4, Ope::Mul, 12},		},
			{"*=1.5d",		{5, Ope::Mul, 1.5},		{5, Ope::Mul, 1.5},		},
			{"*=1.5e4a",	{5, Ope::Mul, 1.5},		{5, Ope::Mul, 1.5},		},	// 指数表記は非サポート
			{"*=0x123ag",	{8, Ope::Mul, 0x123a},	{8, Ope::Mul, 0x123a},	},

		};
		for (auto& t : tbl) {
			std::cout << t.text << std::endl;
			{
				auto r = ParseNum::parse(t.text);
				if (std::get<0>(t.res) == 0) {
					assert(!r);
				} else {
					assert(std::get<0>(t.res) == r->next.data() - t.text.data());
					assert(std::get<1>(t.res) == r->ope);
					assert(std::get<2>(t.res) == r->value);
				}
			}
			{
				auto r = ParseNum::parse(t.text, true);
				if (std::get<0>(t.resOffset) == 0) {
					assert(!r);
				} else {
					assert(std::get<0>(t.resOffset) == r->next.data() - t.text.data());
					assert(std::get<1>(t.resOffset) == r->ope);
					assert(std::get<2>(t.resOffset) == r->value);
				}
			}
		}
	}

	{// parseLength
		static const std::initializer_list<std::pair<std::string, size_t>> list = {
			{"",		480		},
			{"a",		480		},
			{"1",		1920	},
			{"1-!240",	1920 - 240	},
			{"1+8",		1920 + 240	},
			{".",		480 + 240	},
			{"..",		480 + 240 + 120	},
			{"..+",		480 + 240 + 120 + 480},
			{"..+a",	480 + 240 + 120 + 480},
		};
		for (auto i : list) {
			const auto& s = i.first;
			assert(parseLength(s, 480).step == i.second);
		}
	}

	{// parseString
		struct Test {
			std::string src;
			std::optional<std::pair<std::string, size_t>> ans;
		};
		static const std::initializer_list<Test> tbl = {
			{R"()",							std::nullopt	},
			{R"(a)",						std::nullopt	},
			{R"("abc")",					{{"abc",5}}		},
			{R"("abc"de)",					{{"abc",5}}		},
			{R"test(R"(abc)")test",			{{"abc",8}}		},
			{R"test(R"tr1(abc)tr1")test",	{{"abc",14}}	},
		};
		for (const auto& t : tbl) {
			const auto r = parseString(t.src);
			if (!!r != !!t.ans) {
				assert(false);
			} else if (r) {
				assert(r->value == t.ans->first);
				const auto size = r->next.data() - t.src.data();
				assert(size == t.ans->second);
			}
		}
	}

}
