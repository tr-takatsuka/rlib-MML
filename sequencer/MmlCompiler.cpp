#include <charconv>
#include <type_traits>
#include <variant>
#include <optional>
#include <iostream>
#include <typeindex>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

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
		{ErrorCode::createPortShadowError,			u8R"(Port コマンドで参照済みの親のPortと同名の Port を CreatePort しています)"},
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
		{ErrorCode::sequenceRecursionError,			u8R"(Sequence コマンドが再帰呼び出し(無限ループ)になっています)"},
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

	// text の先頭から読めるだけ数値を読む。パースできない場合(先頭が数字でない桁あふれ等)は std::nullopt を返す
	template<class T> auto parseNumber(const std::string_view& text) {
		struct {
			T					value;
			std::string_view	next;	// 数値の次の位置
		}result;
		const auto [ptr, ec] = [&] {
			if constexpr (std::is_floating_point_v<T>) {
				return std::from_chars(text.data(), text.data() + text.size(), result.value, std::chars_format::fixed);	// fixed:指数表記(e)を禁止
			} else {
				return std::from_chars(text.data(), text.data() + text.size(), result.value);
			}
		}();
		if (ec == std::errc{}) {
			result.next = std::string_view(ptr, text.data() + text.size() - ptr);
			return std::optional(result);
		}
		return std::optional<decltype(result)>();
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
		if (!text.empty() && '0' <= text.front() && '9' >= text.front()) {	// 先頭が数字であることだけ確認する("+1.5" や ".5" は対象外)
			if (const auto num = parseNumber<double>(text)) {
				return std::optional(Result{ num->next, num->value });
			}
		}
		return std::optional<Result>();
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
		std::initializer_list<std::string_view> functionNames,				// 関数名
		std::initializer_list<std::string_view> argNames,					// 名前付き引数名(ココにない引数名はエラー)
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
					return std::optional(std::move(result));			// 正常終了
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
						if (auto i = std::find(argNames.begin(), argNames.end(), std::string_view(currentArgName)); i == argNames.end()) {	// 引数名チェック
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
			if (!next.empty() && '0' <= next.front() && next.front() <= '9') {	// 数値か?
				const auto n = parseNumber<size_t>(next);
				if (!n) throw MmlException(MmlCompiler::ErrorCode::lengthError, next);
				constexpr auto maxValue = static_cast<decltype(n->value)>((std::numeric_limits<int>::max)());	// 1項あたりの上限(加算・変換でのオーバーフロー防止)
				if (n->value > maxValue) throw MmlException(MmlCompiler::ErrorCode::lengthError, next);	// 巨大値
				if (stepNotation) {
					tmpStep = static_cast<decltype(tmpStep)>(n->value);
				} else {
					if (n->value == 0) throw MmlException(MmlCompiler::ErrorCode::lengthError, next);	// 0分音符(ゼロ除算防止)
					tmpStep = static_cast<decltype(tmpStep)>((MmlCompiler::timeBase * 4) / n->value);
				}
				next = skipComment(n->next);
			} else {
				if (stepNotation) {		// ステップ数指定しておきながら数値がないなら
					throw MmlException(MmlCompiler::ErrorCode::lengthError, next);
				}
			}

			// 付点音符
			if (next.starts_with('.')) {
				size_t n = 0;
				while (n < next.size() && next[n] == '.') n++;
				size_t t = tmpStep / 2;
				for (size_t j = 0; j < n; j++, t /= 2) tmpStep += t;
				next = skipComment(next.substr(n));		// コメントを読み飛ばす
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

		std::optional<Args> parse(const std::string_view& text, std::initializer_list<std::string_view> functionNames, bool disableArgName) {
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

	struct LessName {
		using is_transparent = void;
		bool operator()(auto&& a, auto&& b) const {
			auto get_name = [](auto&& x) -> std::string_view {
				if constexpr (requires { x.name; }) return x.name;
				else if constexpr (requires { x->name; }) return x->name;
				else if constexpr (requires { x.info.name; }) return x.info.name;
				else return x;
			};
			return get_name(a) < get_name(b);
		}
	};

	struct Sequence {
		const std::string_view	name;	// Sequence名
		const std::string_view	mml;	// mml
		const size_t			align = 0;	// Seq で進む位置の単位(step)。0:Seq内のPositionそのまま。1以上:その倍数になるまで切り上げる
	};
	using SequenceSet = std::set<Sequence, LessName>;

	struct PortInfo {
		const size_t			depth = 0;
		const std::string_view	name;					// Port名
		const std::string_view	instrument;				// instrument
		const uint8_t			channel = 0;			// チャンネル
		const size_t			serial = 0;				// 生成順(Portが生成された順に採番。出力時の並び順に使う)

		size_t					defaultStep = 480;		// デフォルト音長(step)
		int						octave = 4;				// 現在のオクターブ( -2 ～ 8 )
		double					velocity = 100.0;		// 現在のベロシティ(0～127)
	};
	using PortInfoSet = std::set<PortInfo, LessName>;

	struct MmlToEventsParams {
		std::reference_wrapper<const std::vector<std::string_view>>	callStack;	// 呼び出し中のSequence名(ルートは空名"")。size()が階層の深さ
		const std::string_view						mml;
		std::reference_wrapper<const SequenceSet>	sequenceSet;
		std::reference_wrapper<const PortInfoSet>	portInfoSet;
		PortInfoSet::const_iterator					currentPort;
		std::reference_wrapper<size_t>				portSerial;	// Port生成順の採番用カウンタ(再帰呼び出し間で共有)
		size_t depth() const { return callStack.get().size(); }
	};
	struct MmlToEventsResult {
		struct ResPort : public Port {
			size_t depth;
			size_t serial;	// 生成順
		};
		std::vector<ResPort>	ports;
		size_t					endPosition = 0;	// 終了位置
	};
	static MmlToEventsResult mmlToEvents(const MmlToEventsParams& params) {
		// 中間イベント処理関数
		struct Convert {
			// 中間イベント処理。Port毎に独立して処理される。Portの状態は既定値から始まる。
			static void portEvents(EventList& eventList) {
				struct ConvState {
					double expression = 127.0;		// エクスプレッション (0.0～127.0)
					double volume = 100.0;			// ボリューム (0.0～127.0)
					double pan = 64;				// パン (0.0～127.0)
					double pitchBend = 0.0;			// ピッチベンド (-8192～8191)
					double fineTune = 0.0;			// FineTune (-100.0～100.0)
					double coarseTune = 0.0;		// coarseTune (-64.0～63.0)
				}st;
				// 変換して、次に処理するイテレータを返す
				using Handler = EventList::iterator(*)(ConvState&, EventList&, EventList::iterator);
				static const std::map<std::type_index, Handler> map = {
					{typeid(InterPitchBend), [](ConvState& st, EventList&, EventList::iterator it) {
						auto& e = static_cast<const InterPitchBend&>(*it->second);
						st.pitchBend = e.assign.apply(st.pitchBend);
						auto ev = std::make_shared<EventPitchBend>();
						ev->pitchBend = std::clamp(static_cast<int>(std::lround(st.pitchBend)), -8192, 8191);
						it->second = ev;		// Eventを置換
						return std::next(it);
					}},
					{typeid(InterPan), [](ConvState& st, EventList&, EventList::iterator it) {
						auto& e = static_cast<const InterPan&>(*it->second);
						st.pan = e.assign.apply(st.pan);
						auto ev = std::make_shared<EventControlChange>();
						ev->no = static_cast<decltype(ev->no)>(midi::EventControlChange::Type::pan);
						ev->value = std::clamp(static_cast<int>(std::lround(st.pan)), 0, 127);
						it->second = ev;		// Eventを置換
						return std::next(it);
					}},
					{typeid(InterExpression), [](ConvState& st, EventList&, EventList::iterator it) {
						auto& e = static_cast<const InterExpression&>(*it->second);
						st.expression = e.assign.apply(st.expression);
						auto ev = std::make_shared<EventControlChange>();
						ev->no = static_cast<decltype(ev->no)>(midi::EventControlChange::Type::expression);
						ev->value = std::clamp(static_cast<int>(std::lround(st.expression)), 0, 127);
						it->second = ev;		// Eventを置換
						return std::next(it);
					}},
					{typeid(InterVolume), [](ConvState& st, EventList&, EventList::iterator it) {
						auto& e = static_cast<const InterVolume&>(*it->second);
						st.volume = e.assign.apply(st.volume);
						auto ev = std::make_shared<EventControlChange>();
						ev->no = static_cast<decltype(ev->no)>(midi::EventControlChange::Type::volume);
						ev->value = std::clamp(static_cast<int>(std::lround(st.volume)), 0, 127);
						it->second = ev;		// Eventを置換
						return std::next(it);
					}},
					{typeid(InterFineTune), [](ConvState& st, EventList& eventList, EventList::iterator it) {
						auto& e = static_cast<const InterFineTune&>(*it->second);
						st.fineTune = e.assign.apply(st.fineTune);
						const auto toRaw = [](double fineTune) {
							constexpr double inMin = -100.0, inMax = 100.0;
							constexpr int outMax = 16383;
							return std::clamp(static_cast<int>(std::round((fineTune - inMin) * (outMax + 1) / (inMax - inMin))), 0, outMax);	// -8192～0～8192 スケール(+100には微妙に届かない)
						};
						const auto raw = toRaw(st.fineTune);

						struct {
							midi::EventControlChange::Type no;
							uint8_t val;
						}const tbl[] = {
							{midi::EventControlChange::Type::rpnMSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::fineTune) / 0x80 & 0x7f	},
							{midi::EventControlChange::Type::rpnLSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::fineTune) & 0x7f	},
							{midi::EventControlChange::Type::dataEntryMSB,	static_cast<uint8_t>(raw / 0x80 & 0x7f)	},
							{midi::EventControlChange::Type::dataEntryLSB,	static_cast<uint8_t>(raw & 0x7f)	},
						};
						const auto next = std::next(it);
						for (auto& t : tbl) {
							auto e = std::make_shared<EventControlChange>();
							e->no = static_cast<decltype(e->no)>(t.no);
							e->value = t.val;
							eventList.emplace_hint(it, it->first, e);	// Eventを挿入
						}
						eventList.erase(it);	// 不要になったEventを削除
						return next;
					}},
					{typeid(InterCoarseTune), [](ConvState& st, EventList& eventList, EventList::iterator it) {
						auto& e = static_cast<const InterCoarseTune&>(*it->second);
						st.coarseTune = e.assign.apply(st.coarseTune);
						const int val = std::clamp(static_cast<int>(std::lround(st.coarseTune)), -64, 63);
						struct {
							midi::EventControlChange::Type no;
							uint8_t val;
						}const tbl[] = {
							{midi::EventControlChange::Type::rpnMSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::coarseTune) / 0x80 & 0x7f	},
							{midi::EventControlChange::Type::rpnLSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::coarseTune) & 0x7f	},
							{midi::EventControlChange::Type::dataEntryMSB,	static_cast<uint8_t>((val + 64) & 0x7f)	},
							{midi::EventControlChange::Type::dataEntryLSB,	0	},
						};
						const auto next = std::next(it);
						for (auto& t : tbl) {
							auto e = std::make_shared<EventControlChange>();
							e->no = static_cast<decltype(e->no)>(t.no);
							e->value = t.val;
							eventList.emplace_hint(it, it->first, e);	// Eventを挿入
						}
						eventList.erase(it);	// 不要になったEventを削除
						return next;
					}},
				};
				for (auto it = eventList.begin(); it != eventList.end();) {
					const auto i = map.find(typeid(*it->second));
					it = (i != map.end()) ? (i->second)(st, eventList, it) : std::next(it);
				}
			}

			// Port を跨いで値を引き継ぐ中間イベントを最終イベントへ変換する(ルートで全Portが揃った後に1回だけ呼ぶ)
			//   Tempo        : 曲全体で1つの値を引き継ぐ
			//   MasterVolume : 同じ instrument の全Portで1つの値を引き継ぐ
			// 全Portのイベントを (位置, Portの並び順, Port内のイベント順) の順に処理する
			static void globalEvents(std::vector<MmlToEventsResult::ResPort>& ports) {
				struct Target {
					EventList::iterator	it;
					std::string_view	instrument;
				};
				std::multimap<size_t, Target> targets;
				for (auto& port : ports) {
					for (auto it = port.eventList.begin(); it != port.eventList.end(); ++it) {
						const auto& id = typeid(*it->second);
						if (id == typeid(InterTempo) || id == typeid(InterMasterVolume)) targets.emplace(it->first, Target{ it, port.instrument });
					}
				}
				double tempo = 120.0;							// テンポ(曲全体)
				std::map<std::string_view, double> masterVolumes;	// マスターボリューム(instrument毎)
				for (auto& [position, target] : targets) {
					const auto it = target.it;
					if (typeid(*it->second) == typeid(InterTempo)) {
						auto& e = static_cast<const InterTempo&>(*it->second);
						tempo = e.assign.apply(tempo);
						assert(std::isfinite(tempo));
						auto ev = std::make_shared<EventMeta>();
						auto t = midi::EventMeta::createTempo(std::clamp(tempo, 1.0, 1000.0));	// createTempoを利用
						ev->type = static_cast<decltype(ev->type)>(t.type);
						ev->data = t.data;
						it->second = ev;		// Eventを置換
					} else {
						auto& e = static_cast<const InterMasterVolume&>(*it->second);
						double& masterVolume = masterVolumes.try_emplace(target.instrument, 16383.0).first->second;
						masterVolume = e.assign.apply(masterVolume);
						auto ev = std::make_shared<EventSystemExclusive>();
						midi::utility::Bit14 u(static_cast<uint16_t>(std::clamp(static_cast<int>(std::lround(masterVolume)), 0, 16383)));
						ev->data = { 0xf0, 0x7f, 0x7f, 0x04, 0x1, static_cast<uint8_t>(u.lsb), static_cast<uint8_t>(u.msb), 0xf7 };
						it->second = ev;		// Eventを置換
					}
				}
			}
		};

		struct PortState : public PortInfo {
			size_t						position = 0;			// 現在の位置
			std::shared_ptr<EventNote>	beforeEvent;			// 直前の音符( ^の対象)
			bool						noteUnmove = false;		// Noteで現在位置を進めないモード
			bool						referenced = false;		// Port コマンドで(親から引き継いだPortとして)参照された
			EventList					eventList;				// 出力 <position,Event>
		};
		struct State {
			const MmlToEventsParams& params;
			SequenceSet						sequenceSet;
			std::set<PortState, LessName>	portStateSet;
			decltype(portStateSet)::iterator currentPort;
			std::vector<MmlToEventsResult::ResPort>	resultPorts;		// 結果Port
			size_t							maxPosition = 0;	// portStateSet から外れたPort(隠蔽された親のPort)の終了位置の最大値
			std::vector<Result::Error>		errors;
			PortState& getCurrentPort() {
				return const_cast<PortState&>(*currentPort);
			};
			void addErrors(const std::vector<Result::Error>& errs) {	// 重複(同じ位置・同じコード)を除いて追記
				for (const auto& e : errs) {
					const bool exists = std::any_of(errors.begin(), errors.end(), [&](const Result::Error& x) {
						return x.code == e.code && x.text.data() == e.text.data() && x.text.size() == e.text.size();
					});
					if (!exists) errors.push_back(e);
				}
			}
		}state{ params };
		state.currentPort = state.portStateSet.insert(PortState{ *params.currentPort }).first;

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
				auto& port = state.getCurrentPort();
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
				auto& port = state.getCurrentPort();
				const auto len = parseLength(next, port.defaultStep);
				port.position += len.step;
				port.beforeEvent.reset();				// '^'の対象をクリア
				return len.next;
			}},

			// a～g?? 音符
			{ErrorCode::noteCommandRangeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
#if 0
				static const regex re(R"(^([a-g][\+\-]?))");
				const auto m = regexSearch(text, re);
				if (!m) return std::nullopt;
				auto next = std::string_view((*m)[0].second, text.end());
				next = skipComment(next);			// コメントを読み飛ばす
				auto& port = state.getCurrentPort();
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
						if (note >= 0 && note <= 127) return note;		// 範囲チェック
					}
					throw MmlException(ErrorCode::noteCommandRangeError, text);
				}();
#else
				if (text.empty()) return std::nullopt;
				const char c = text.front();
				if (c < 'a' || c > 'g') return std::nullopt;
				static constexpr int baseNote[] = { 9, 11, 0, 2, 4, 5, 7 };	// a,b,c,d,e,f,g
				size_t noteLen = 1;
				int accidental = 0;
				if (text.size() > 1 && text[1] == '+') {
					accidental = 1;
					noteLen = 2;
				} else if (text.size() > 1 && text[1] == '-') {
					accidental = -1;
					noteLen = 2;
				}
				auto next = text.substr(noteLen);
				next = skipComment(next);			// コメントを読み飛ばす
				auto& port = state.getCurrentPort();
				const auto len = parseLength(next, port.defaultStep);
				auto e = std::make_shared<EventNote>();
				e->note = [&] {
					const int note = (port.octave + 2) * 12 + baseNote[c - 'a'] + accidental;
					if (note >= 0 && note <= 127) return note;		// 範囲チェック
					throw MmlException(ErrorCode::noteCommandRangeError, text);
				}();
#endif
				e->length = len.step;
				e->velocity = std::clamp(static_cast<int>(std::lround(port.velocity)), 0, 127);
				port.eventList.emplace(port.position, e);
				if (!port.noteUnmove) {
					port.position += len.step;
				}
				port.beforeEvent = std::move(e);
				return len.next;
			}},

			// < > オクターブUPDOWN
			{ErrorCode::octaveUpDownCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto check = [&](int n) {
					auto& port = state.getCurrentPort();
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
				auto& port = state.getCurrentPort();
				port.velocity = r2->apply(port.velocity);
				return r2->next;
			}},

			// o?? オクターブ
			{ErrorCode::oCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, 'o');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);			// コメントを読み飛ばす
				const auto octave = parseNumber<int>(next);	// "-"のみ許容("+"は不可)
				if (!octave || octave->value < -2 || octave->value > 8) {
					throw MmlException(ErrorCode::oCommandRangeError, text);
				}
				auto& port = state.getCurrentPort();
				port.octave = octave->value;
				port.beforeEvent.reset();				// '^'の対象をクリア
				return octave->next;
			}},

			// l?? デフォルト音長
			{ErrorCode::lCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, 'l');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);			// コメントを読み飛ばす
				auto& port = state.getCurrentPort();
				const auto len = parseLength(next, port.defaultStep);
				port.defaultStep = len.step;
				return len.next;
			}},

			// ' noteで位置更新するか否かモード
			{ErrorCode::unknownError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, '\'');
				if (!r) return std::nullopt;
				auto& port = state.getCurrentPort();
				port.noteUnmove = !port.noteUnmove;		// 反転
				port.beforeEvent.reset();				// '^'の対象をクリア
				return *r;
			}},

			// @? プログラムチェンジ
			{ErrorCode::programchangeCommandError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = isStartsWith(text, '@');
				if (!r) return std::nullopt;
				auto next = skipComment(*r);			// コメントを読み飛ばす
				const auto programNo = parseNumber<unsigned int>(next);
				if (!programNo || programNo->value > 127) throw MmlException(ErrorCode::programchangeCommandError, next);
				auto& port = state.getCurrentPort();
				auto e = std::make_shared<EventProgramChange>();
				e->programNo = static_cast<decltype(e->programNo)>(programNo->value);
				port.eventList.emplace(port.position, e);
				return programNo->next;
			}},

			// t?? テンポ
			{ErrorCode::tCommandRangeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
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
				auto& port = state.getCurrentPort();
				port.eventList.emplace(port.position, e);
				return r2->next;
			}},

			// Volume
			{ErrorCode::volumeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "V","Volume" }, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = state.getCurrentPort();
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
						port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			}},

			// Expression
			{ErrorCode::expressionError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "Ep","Expression"}, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = state.getCurrentPort();
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
						port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			}},

			// ControlChange
			{ErrorCode::controlChangeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
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
				auto& port = state.getCurrentPort();
				auto e = std::make_shared<EventControlChange>();
				e->no = static_cast<decltype(e->no)>(no);
				e->value = static_cast<decltype(e->value)>(val);
				port.eventList.emplace(port.position, e);
				return r->next;
			}},

			// PitchBend
			{ErrorCode::pitchBendError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "PitchBend" }, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = state.getCurrentPort();
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
						port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			}},

			// Pan
			{ErrorCode::panError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "Pan"}, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = state.getCurrentPort();
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
						port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			}},

			// Port
			{ErrorCode::portError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "Port" }, {});
				if (!r) return std::nullopt;
				const auto name = (*r).findArgString(0).value_or("");
				if (name.empty()) throw MmlException(ErrorCode::portNameError, text);
				auto it = state.portStateSet.find(name);
				if (it == state.portStateSet.end()) {			// 存在しないなら
					const auto i = state.params.portInfoSet.get().find(name);	// 親から検索
					if (i == state.params.portInfoSet.get().end()) throw MmlException(ErrorCode::portNameError, text);	// それでも存在しないならエラー
					it = state.portStateSet.insert(PortState(*i)).first;
				}
				if (it->depth < state.params.depth()) const_cast<PortState&>(*it).referenced = true;	// 親のPortを参照した(以降、同名の CreatePort は意味が変わるのでエラー)
				state.currentPort = it;		// 成功してから代入する(エラー時に currentPort が end() にならないように)
				return r->next;
			}},

			// CreatePort
			{ErrorCode::createPortError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto r = parseFunction(text, { "CreatePort" }, { "name","instrument","channel" });
				if (!r) return std::nullopt;
				const auto name = (*r).findArgString("name").value_or("");
				if (name.empty()) throw MmlException(ErrorCode::createPortPortNameError, text);
				const auto instrument = (*r).findArgString("instrument").value_or(state.getCurrentPort().instrument);	// 省略したらカレントのinstrumentを採用する
				const auto ch = r->findArg<uintmax_t>("channel").second;
				if (!ch || *ch < 1 || *ch > 16) throw MmlException(ErrorCode::createPortChannelError, text);
				if (const auto it = state.portStateSet.find(name); it != state.portStateSet.end()) {	// 既に存在してる？
					if (it->depth >= state.params.depth()) throw MmlException(ErrorCode::createPortDuplicateError, text); // それが親の持ち物ではないなら、同名が構築済としてエラー
					if (it->referenced) throw MmlException(ErrorCode::createPortShadowError, text);	// 親のPortとして参照済みの名前を作り直すと、同じ名前が位置で別のPortを指すのでエラー
					auto nh = state.portStateSet.extract(it);
					auto& p = nh.value();
					state.maxPosition = std::max(state.maxPosition, p.position);	// 外れるPortの位置も終了位置の対象
					state.resultPorts.emplace_back(MmlToEventsResult::ResPort{ p.name, p.instrument, p.channel, std::move(p.eventList), p.depth, p.serial });
				}
				const auto channel = static_cast<uint8_t>(*ch - 1);
				// resultPorts に溜まっている 同name,同instrument,同channel のPortがあれば、それを引き継ぐ(1つのPortに結合する)
				std::optional<EventList> adopted;
				size_t serial = 0;
				if (const auto ri = std::find_if(state.resultPorts.begin(), state.resultPorts.end(), [&](const auto& rp) { return rp.name == name && rp.instrument == instrument && rp.channel == channel; }); ri != state.resultPorts.end()) {
					adopted = std::move(ri->eventList);
					serial = ri->serial;	// 最初に生成されたPortの生成順を保つ
					state.resultPorts.erase(ri);
				}
				if (!adopted) serial = state.params.portSerial.get()++;
				state.currentPort = state.portStateSet.emplace(PortState({ PortInfo({ state.params.depth(), name, instrument, channel, serial }) })).first;	// port追加
				if (adopted) const_cast<EventList&>(state.currentPort->eventList) = std::move(*adopted);
				return r->next;
			}},

			// CreateSequence
			{ErrorCode::createSequenceError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "CreateSeq","CreateSequence" }, { "name","mml","align" });
				if (!r) return std::nullopt;
				const auto name = (*r).findArgString("name").value_or("");
				if (name.empty()) throw MmlException(ErrorCode::createSequenceNameError, text);
				const auto mml = (*r).findArg<std::string_view>("mml").second;
				if (!mml) throw MmlException(ErrorCode::createSequenceError, text);
				const auto align = [&]()->size_t {
					const auto arg = (*r).findArg<std::string_view>("align");
					if (!arg.first) return 0;	// 指定ナシ(Seq内のPositionそのまま)
					if (!arg.second) throw MmlException(ErrorCode::createSequenceError, text);	// 型が違う
					const auto& alignText = *arg.second;
					const auto len = parseLength(alignText, state.getCurrentPort().defaultStep);	// 音長と同じ書式 (例: "1"=全音符,"4"=4分音符,"2."=付点2分音符)
					if ((len.next.data() - alignText.data()) != alignText.size() || len.step == 0) throw MmlException(ErrorCode::createSequenceError, text);	// 余計な文字列がある / 0
					return len.step;
				}();
				auto r2 = state.sequenceSet.emplace(Sequence({ name, *mml, align }));						// Sequence追加
				if (!r2.second) throw MmlException(ErrorCode::createSequenceDuplicateError, text);	// 既にあるならエラー
				return r->next;
			}},

			// Sequence
			{ErrorCode::sequenceError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "Seq","Sequence" }, { "length" });
				if (!r) return std::nullopt;
				const auto name = (*r).findArgString(0).value_or("");
				if (name.empty()) throw MmlException(ErrorCode::sequenceNameError, text);
				const auto seq = [&] {
					if (auto i = state.sequenceSet.find(name); i != state.sequenceSet.end()) {	// ローカルに存在？
						return *i;
					}
					if (auto i = state.params.sequenceSet.get().find(name); i != state.params.sequenceSet.get().end()) {	// 親に存在？
						return *state.sequenceSet.emplace(*i).first;	// ローカルに複製する(同じ名前でCreateさせない)
					}
					throw MmlException(ErrorCode::sequenceNameError, text);
				}();

				auto callStack = state.params.callStack.get();
				for (auto& c : callStack) if (c.data() == seq.name.data()) throw MmlException(ErrorCode::sequenceRecursionError, text);	// 無限再帰チェック
				callStack.push_back(seq.name);	// 子に渡すcallStack(自分を追加)
				auto& port = state.getCurrentPort();

				const auto length = [&]()->std::optional<size_t> {
					const auto arg = (*r).findArg<std::string_view>("length");
					if (!arg.first) return std::nullopt;	// 指定ナシ
					if (!arg.second) throw MmlException(ErrorCode::sequenceLengthError, text);	// 型が違う
					const auto& lengthText = *arg.second;
					const auto len = parseLength(lengthText, port.defaultStep);
					if ((len.next.data() - lengthText.data()) != lengthText.size()) throw MmlException(ErrorCode::sequenceLengthError, text);	// 余計な文字列がある
					return len.step;
				}();

				auto result = [&]()->MmlToEventsResult {
					SequenceSet sequenceSet = state.sequenceSet;														// ローカル優先
					sequenceSet.insert(state.params.sequenceSet.get().begin(), state.params.sequenceSet.get().end());	// 次に親
					PortInfoSet portInfoSet;
					for (const auto& i : state.portStateSet) portInfoSet.insert(i);										// ローカル優先
					portInfoSet.insert(state.params.portInfoSet.get().begin(), state.params.portInfoSet.get().end());	// 次に親
					const auto it = portInfoSet.find(port.name);
					try {
						return mmlToEvents({ callStack, seq.mml, sequenceSet,portInfoSet,it, state.params.portSerial });	// sequence(mml)をパース
					} catch (const MmlException& e) {
						state.addErrors(e.errors);	// エラーは重複を除いて追記し、処理は継続する
						return {};
					}
				}();

				for (auto& resPort : result.ports) {
					if (length) resPort.eventList.erase(resPort.eventList.lower_bound(*length), resPort.eventList.end());	// lenth を越えるイベントは破棄
					if (resPort.eventList.empty()) continue;	// 空なら次へ

					{// 現在位置を加算　(std::multiset の key をconst_castで書き換えるのを避ける)
						// for (auto& ev : resPort.eventList) const_cast<size_t&>(ev.first) += port.position;
						EventList shifted;
						while (!resPort.eventList.empty()) {
							auto nh = resPort.eventList.extract(resPort.eventList.begin());
							nh.key() += port.position;
							shifted.insert(shifted.end(), std::move(nh));
						}
						resPort.eventList = std::move(shifted);
					}

					if (state.params.depth() < resPort.depth) {	// Seq の中で生成されたPortは resultPorts に
						// 同name,同instrument,同channel のPortがあれば結合(SMFのTrackを増やさない)。結合先は書き込み順に後ろへ追加される
						const auto same = [&](const auto& p) { return p.name == resPort.name && p.instrument == resPort.instrument && p.channel == resPort.channel; };
						if (const auto li = state.portStateSet.find(resPort.name); li != state.portStateSet.end() && same(*li)) {
							const_cast<EventList&>(li->eventList).merge(resPort.eventList);
						} else if (const auto ri = std::find_if(state.resultPorts.begin(), state.resultPorts.end(), same); ri != state.resultPorts.end()) {
							ri->eventList.merge(resPort.eventList);
						} else {
							state.resultPorts.emplace_back(std::move(resPort));
						}
					} else {									// portStateSet に合成
						auto it = state.portStateSet.find(resPort.name);
						if (it == state.portStateSet.end()) {
							const auto i = state.params.portInfoSet.get().find(resPort.name);
							if (i == state.params.portInfoSet.get().end()) throw MmlException(ErrorCode::unknownError, text);	// failsafe
							it = state.portStateSet.insert(PortState(*i)).first;
						}
						const_cast<EventList&>(it->eventList).merge(resPort.eventList);
					}
				}
				port.beforeEvent.reset();		// '^'の対象をクリア

				if (!port.noteUnmove) {
					if (length) {
						port.position += *length;	// length 指定が最優先
					} else if (seq.align) {			// CreateSeq の align 指定があれば、その倍数になるまで切り上げる
						port.position += (result.endPosition + seq.align - 1) / seq.align * seq.align;
					} else {						// 指定ナシはSeq内のPositionそのまま
						port.position += result.endPosition;
					}
				}

				return r->next;
			}},

			// FineTune
			{ErrorCode::fineTuneError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "FineTune"}, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = state.getCurrentPort();
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
						port.eventList.emplace(port.position, e);
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
						auto& port = state.getCurrentPort();
						const auto r = ParseNum::parse(argValue);
						if (!r) throw MmlException(MmlCompiler::ErrorCode::coarseTuneRangeError, argValue);
						if (r->ope == ParseNum::Ope::Set) {	// 絶対指定なら
							if (std::get_if<double>(&r->value)) throw MmlException(ErrorCode::panRangeError, r->matched);	// 浮動小数はエラー
						}
						const double n = r->getValue();
						if (n < -64 || n > 63) throw MmlException(ErrorCode::coarseTuneRangeError, r->matched);
						auto e = std::make_shared<InterCoarseTune>();
						e->assign = *r;
						port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			}},

			// MasterVolume
			{ErrorCode::masterVolumeError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				const auto args = ParseFunc::parse(text, { "MasterVolume"}, true);
				if (!args) return std::nullopt;
				const auto next = args->parse([&](const auto& argKey, const std::string_view& argValue) {
					if (auto argIndex = std::get_if<std::size_t>(&argKey)) {
						if (*argIndex > 0) throw MmlException(MmlCompiler::ErrorCode::argumentError, argValue);	// 名前ナシ引数は1つまで
						auto& port = state.getCurrentPort();
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
						port.eventList.emplace(port.position, e);
						return r->next;
					}
					assert(false);
					return argValue;
				});
				return next;
			}},

			// Meta
			{ErrorCode::metaError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "Meta" }, { "type" });
				if (!r) return std::nullopt;
				const auto type = (*r).findArgInt("type");
				if (!type || *type < 0 || *type > std::numeric_limits<uint8_t>::max()) {
					throw MmlException(ErrorCode::metaTypeError, text);
				}
				auto& port = state.getCurrentPort();
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
				port.eventList.emplace(port.position, e);
				return r->next;
			}},

			// SysEx システムエクスクルーシブ
			{ErrorCode::sysExError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				auto r = parseFunction(text, { "SysEx" }, {});
				if (!r) return std::nullopt;
				auto& port = state.getCurrentPort();
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
				port.eventList.emplace(port.position, e);
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
				auto& port = state.getCurrentPort();
				auto e = std::make_shared<EventMeta>();
				e->type = static_cast<decltype(e->type)>(midi::EventMeta::Type::sequencerLocal);
				auto stringified = j.stringify();
				e->data = { stringified.begin(), stringified.end() };
				auto it = port.eventList.emplace(port.position, e);
				return r->next;
			}},

			// DefinePresetPSG PSG(SSG)音色定義(rlib-MML 固有メタイベント)
			{ErrorCode::definePresetPSGError,[](State& state,const std::string_view& text)->std::optional<std::string_view> {
				// AR,HR,DR,RR	アタック/ホールド/ディケイ/リリース時間(秒、0～60)
				// SL			サステインレベル(0.0～1.0)
				// noise		ノイズ周波数(0:OFF, 1～31)
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
				{// noise (0～31)
					const auto n = (*r).findArgInt(envRange.size());
					if (!n) throw MmlException(ErrorCode::definePresetPSGError, text);
					if (*n < 0 || *n > 31) throw MmlException(ErrorCode::definePresetPSGRangeError, text);
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
				auto& port = state.getCurrentPort();
				auto e = std::make_shared<EventMeta>();
				e->type = static_cast<decltype(e->type)>(midi::EventMeta::Type::sequencerLocal);
				auto stringified = j.stringify();
				e->data = { stringified.begin(), stringified.end() };
				auto it = port.eventList.emplace(port.position, e);
				return r->next;
			}},

		};

		for (auto next = params.mml; true;) {
			try {
				next = skipComment(next);					// コメントを読み飛ばす
				if (next.empty()) break;					// 完了?
				auto i = parsers.begin();
				for (; i != parsers.end(); i++) {
					try {
						if (const auto r = i->func(state, next)) {
							next = *r;
							break;
						}
					} catch (const MmlException&) {
						throw;
					} catch (...) {
						throw MmlException(i->errorCode, next);
					}
				}
				if (i == parsers.end()) throw MmlException(ErrorCode::unknownError, next);	// 未定義文字列エラー
			} catch (MmlException& e) {
				state.errors.insert(state.errors.end(), e.errors.begin(), e.errors.end());

				// 行末まで飛ぶ(改行が無ければ末尾まで飛ぶ)
				next.remove_prefix(std::distance(next.begin(), std::find_if(next.begin(), next.end(), [](unsigned char ch) {	// 先頭の空白を読み飛ばす
					return !std::isspace(ch); // return !std::isspace(ch, std::locale::classic()); gccでNG
				})));
				if (auto pos = next.find_first_of("\r\n"); pos != std::string_view::npos) {
					if (pos == 0) {
						next.remove_prefix(next.starts_with("\r\n") ? 2 : 1);
					} else {
						next = next.substr(pos);
					}
				} else {
					next = {};
				}

			}
		}
		if (state.errors.size() > 0) throw MmlException(std::move(state.errors));

		const bool isRoot = params.depth() == 1;
		size_t endPosition = state.maxPosition;	// このシーケンスの終了位置
		for (auto& port : state.portStateSet) {
			auto& p = const_cast<PortState&>(port);	// const_cast することで std::move 可にする
			endPosition = std::max(endPosition, p.position);
			if (p.depth >= params.depth() || isRoot) Convert::portEvents(p.eventList);	// 自分の階層で生成したPort(とルート)は中身が揃ったので中間イベントを処理。親から引き継いだPortは親に任せる。
			state.resultPorts.emplace_back(MmlToEventsResult::ResPort{ p.name, p.instrument, p.channel, std::move(p.eventList), p.depth, p.serial });
		}
		if (isRoot) {		// 全Portが揃ったので、Port生成順に並べて Port を跨ぐ中間イベントを変換する
			std::stable_sort(state.resultPorts.begin(), state.resultPorts.end(), [](const auto& a, const auto& b) { return a.serial < b.serial; });
			Convert::globalEvents(state.resultPorts);
		}
		return MmlToEventsResult{ std::move(state.resultPorts), endPosition };
	}

};

MmlCompiler::Result MmlCompiler::compile(const std::shared_ptr<const std::string>& mml) {
	Result r{ mml };
	try {
		const Inner::PortInfoSet portInfoSet = { {0,std::string_view(),std::string_view(),0,0} };
		const Inner::SequenceSet sequenceSet;
		const std::vector<std::string_view> callStack = { std::string_view() };	// ルート(空名)
		size_t portSerial = portInfoSet.cbegin()->serial + 1;	// 0 はルートの既定Port(名前なし)。以降 CreatePort された順に採番
		auto result = Inner::mmlToEvents({ callStack, *mml, sequenceSet, portInfoSet, portInfoSet.cbegin(), portSerial });
		r.ports.reserve(result.ports.size());
		for (auto& port : result.ports) r.ports.emplace_back(std::move(port));
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

	{// parseWord (浮動小数点数パース)
		std::cout << "parseWord" << std::endl;
		struct Test {
			std::string src;
			std::optional<std::pair<double, size_t>> ans;	// {値,消費文字数} nullopt:word解釈失敗
		};
		const std::initializer_list<Test> tbl = {
			{"1.5",						{{1.5, 3}}		},
			{"0.25abc",					{{0.25, 4}}		},
			{"3.",						{{3.0, 2}}		},
			{std::string(320, '9'),			std::nullopt	},	// 桁あふれ
			{std::string(320, '9') + ".5",	std::nullopt	},	// 桁あふれ
		};
		for (const auto& t : tbl) {
			std::cout << t.src.substr(0, 32) << std::endl;
			const auto r = MmlCompiler::Util::parseWord(t.src);
			if (!t.ans) {
				assert(!r || !r->word);	// word として解釈できない(クラッシュしないことが重要)
			} else {
				assert(r && r->word);
				const auto* p = std::get_if<double>(&*r->word);
				assert(p);
				assert(*p == t.ans->first);
				assert(static_cast<size_t>(r->next.data() - t.src.data()) == t.ans->second);
			}
		}
	}

	{// parseLength 桁あふれ
		std::cout << "parseLength (overflow)" << std::endl;
		const std::string hugeLength(30, '9');	// size_t の範囲を超える桁数
		bool caught = false;
		try {
			parseLength(hugeLength, 480);
		} catch (const MmlException& e) {
			caught = true;
			assert(e.errors.size() == 1);
			assert(e.errors[0].code == MmlCompiler::ErrorCode::lengthError);
		}
		assert(caught);	// MmlExceptionとして捕捉できる(未処理例外にならない)ことを確認
	}

	{// compile(): oコマンド/@コマンドの桁あふれ
		std::cout << "compile (o/@ command overflow)" << std::endl;
		const std::string huge(30, '9');	// int の範囲を超える桁数

		// オクターブ指定の桁あふれ -> oCommandRangeErrorとして捕捉されること
		{
			const auto r = MmlCompiler::compile("CreatePort(name:p,instrument:i,channel:1) o" + huge);
			assert(r.hasError());
			assert(!r.errors.empty() && r.errors[0].code == MmlCompiler::ErrorCode::oCommandRangeError);
		}
		// オクターブ指定の正常系が引き続き動作すること(リグレッション確認)
		{
			const auto r = MmlCompiler::compile(std::string("CreatePort(name:p,instrument:i,channel:1) o5"));
			assert(!r.hasError());
		}
		// プログラムチェンジの桁あふれ -> programchangeCommandErrorとして捕捉されること
		{
			const auto r = MmlCompiler::compile("CreatePort(name:p,instrument:i,channel:1) @" + huge);
			assert(r.hasError());
			assert(!r.errors.empty() && r.errors[0].code == MmlCompiler::ErrorCode::programchangeCommandError);
		}
		// プログラムチェンジの正常系が引き続き動作すること(リグレッション確認)
		{
			const auto r = MmlCompiler::compile(std::string("CreatePort(name:p,instrument:i,channel:1) @1"));
			assert(!r.hasError());
		}
	}

	{// compile(): Seq / Port / Sequence 関連
		std::cout << "compile (Seq/Port)" << std::endl;
		using Code = MmlCompiler::ErrorCode;
		const auto comp = [](const std::string& mml) { return MmlCompiler::compile(mml); };
		const auto portCount = [](const MmlCompiler::Result& r, std::string_view portName) {
			return std::count_if(r.ports.begin(), r.ports.end(), [&](const MmlCompiler::Port& p) { return p.name == portName; });
		};
		const auto codes = [](const MmlCompiler::Result& r) {
			std::vector<Code> v;
			for (const auto& e : r.errors) v.push_back(e.code);
			return v;
		};
		const std::string head = "CreatePort(name:a,instrument:fm,channel:1)\n";
		// イベントを文字列にする(テストの期待値を読みやすくするため)
		//   音符                : "o4c@0:480v100" = オクターブ4の c / 位置0 / 音長480 / ベロシティ100 (MMLの音名。#は + )
		//   プログラムチェンジ  : "P33@0"         = 音色33 / 位置0
		//   コントロールチェンジ: "C7=110@0"      = No.7 値110 / 位置0
		const auto eventText = [](size_t position, const MmlCompiler::EventBase& event) -> std::string {
			static const char* const noteNames[] = { "c", "c+", "d", "d+", "e", "f", "f+", "g", "g+", "a", "a+", "b" };
			const auto at = "@" + std::to_string(position);
			if (const auto n = dynamic_cast<const MmlCompiler::EventNote*>(&event)) {
				return "o" + std::to_string(n->note / 12 - 2) + noteNames[n->note % 12] + at + ":" + std::to_string(n->length) + "v" + std::to_string(n->velocity);
			} else if (const auto pc = dynamic_cast<const MmlCompiler::EventProgramChange*>(&event)) {
				return "P" + std::to_string(pc->programNo) + at;
			} else if (const auto cc = dynamic_cast<const MmlCompiler::EventControlChange*>(&event)) {
				return "C" + std::to_string(cc->no) + "=" + std::to_string(cc->value) + at;
			}
			return "?" + at;
		};
		// 指定名のPort(同名が複数あれば順に連結)の全イベントを文字列列にする
		const auto evs = [&](const MmlCompiler::Result& r, std::string_view portName) {
			std::vector<std::string> v;
			for (const auto& p : r.ports) {
				if (p.name != portName) continue;
				for (const auto& ev : p.eventList) v.push_back(eventText(ev.first, *ev.second));
			}
			return v;
		};
		// 指定名のPort(同名が複数あれば全て)の音符だけを、位置順(同位置は出力順)の文字列列にする
		const auto notes = [&](const MmlCompiler::Result& r, std::string_view portName) {
			std::vector<std::pair<size_t, std::string>> v;
			for (const auto& p : r.ports) {
				if (p.name != portName) continue;
				for (const auto& ev : p.eventList) if (dynamic_cast<const MmlCompiler::EventNote*>(ev.second.get())) v.emplace_back(ev.first, eventText(ev.first, *ev.second));
			}
			std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			std::vector<std::string> r2;
			for (auto& x : v) r2.push_back(std::move(x.second));
			return r2;
		};
		using Strs = std::vector<std::string>;
	// 名前のあるPortの名前を出力順に返す
		const auto names = [](const MmlCompiler::Result& r) {
			std::vector<std::string> v;
			for (const auto& p : r.ports) if (!p.name.empty()) v.emplace_back(p.name);
			return v;
		};
		using Names = std::vector<std::string>;

		{// o / l / v が Seq の中に引き継がれる
			const auto r = comp(head + "CreateSeq(name:A,mml:\"c\")\no5 l8 v50 Seq(A)");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o5c@0:240v50" }));
		}
		{// Seq の中での o / l / v の変更は呼び出し側に漏れない
			const auto r = comp(head + "CreateSeq(name:X,mml:\"o2 l16 v10 c\")\nSeq(X) c");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o2c@0:120v10", "o4c@120:480v100" }));
		}
		{// ネストした Seq にも引き継がれる(2段)。中間での変更は更に内側へ、外側へは漏れない
			const auto r = comp(head + "CreateSeq(name:X,mml:\"c\")\nCreateSeq(name:Y,mml:\"o6 l8 v33 Seq(X) c\")\nSeq(Y) c");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o6c@0:240v33", "o6c@240:240v33", "o4c@480:480v100" }));
		}
		{// ネストした Seq の中で祖先の Port を使う(親は一度も触れていないPort)。Seq内で触れたPort(a)の位置も終了位置に含まれるので、b の d は In の終了位置(480)から
			const auto r = comp(head + "CreatePort(name:b,instrument:fm,channel:2)\n"
				"CreateSeq(name:In,mml:\"Port(a) o6 c\")\nCreateSeq(name:Out,mml:\"Seq(In) d\")\nPort(b) Seq(Out)");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o6c@0:480v100" }));
			assert((notes(r, "b") == Strs{ "o4d@480:480v100" }));
		}
		{// 親で状態(o6)を変更した別Portは、Seq の中の Port() で状態ごと引き継がれる
			const auto r = comp(head + "CreatePort(name:b,instrument:psg,channel:2)\nCreateSeq(name:X,mml:\"Port(b) c\")\nPort(b) o6 Port(a) Seq(X)");
			assert(!r.hasError());
			assert((notes(r, "b") == Strs{ "o6c@0:480v100" }));
			assert(notes(r, "a").empty());
		}
		{// Seq は現在位置から展開される
			const auto r = comp(head + "CreateSeq(name:X,mml:\"c\")\nr4 Seq(X)");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o4c@480:480v100" }));
		}
		{// 自己再帰は無限ループにならず sequenceRecursionError
			const auto r = comp(head + "CreateSeq(name:A,mml:\"Seq(A)\")\nSeq(A) c");
			assert((codes(r) == std::vector<Code>{ Code::sequenceRecursionError }));
		}
		{// 相互再帰(A→B→A)も sequenceRecursionError(同じ箇所のエラーは1件にまとめられる)
			const auto r = comp(head + "CreateSeq(name:A,mml:\"Seq(B)\")\nCreateSeq(name:B,mml:\"Seq(A)\")\nSeq(A)");
			assert((codes(r) == std::vector<Code>{ Code::sequenceRecursionError }));
		}
		{// 再帰ではない深い呼び出し(60段)は上限なく展開できる
			std::string mml = head + "CreateSeq(name:S0,mml:\"c\")\n";
			for (int i = 1; i < 60; i++) mml += "CreateSeq(name:S" + std::to_string(i) + ",mml:\"Seq(S" + std::to_string(i - 1) + ")\")\n";
			mml += "Seq(S59)";
			const auto r = comp(mml);
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o4c@0:480v100" }));
		}
		{// 同名でも別定義のSequenceは再帰とみなさない(Seq内で同名を再構築して呼ぶ)
			const auto r = comp(head + "CreateSeq(name:X,mml:\"c\")\n"
				"CreateSeq(name:Y,mml:R\"B(CreateSeq(name:X,mml:\"d\") Seq(X))B\")\nSeq(Y)");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o4d@0:480v100" }));
		}
		{// 同じSequenceを続けて呼ぶのは再帰ではない。Seq内のPositionそのまま(c の音長480)進む
			const auto r = comp(head + "CreateSeq(name:X,mml:\"c\")\nSeq(X) Seq(X)");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o4c@0:480v100", "o4c@480:480v100" }));
		}
		{// length / align 省略時はSeq内のPositionそのまま進む(c1 c1 は2小節)
			const auto r = comp(head + "CreateSeq(name:X,mml:\"c1 c1\")\nSeq(X) d");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o4c@0:1920v100", "o4c@1920:1920v100", "o4d@3840:480v100" }));
		}
		{// length 指定: それ以降のイベントは捨てられ、その長さだけ進む
			const auto r = comp(head + "CreateSeq(name:X,mml:\"cdefgab\")\nSeq(X,length:\"4\") e");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o4c@0:480v100", "o4e@480:480v100" }));
		}
		{// 空のSequenceは進まない / 推奨は CreateSeq・Seq。別名 CreateSequence・Sequence も同じ意味で使える
			const auto r1 = comp(head + "CreateSeq(name:E,mml:\"\")\nSeq(E) c");
			assert(!r1.hasError());
			assert((notes(r1, "a") == Strs{ "o4c@0:480v100" }));
			const auto r2 = comp(head + "CreateSequence(name:E,mml:\"\")\nSequence(E) c");
			assert(!r2.hasError());
			assert((notes(r2, "a") == Strs{ "o4c@0:480v100" }));
			const auto r3 = comp(head + "CreateSequence(name:E,mml:\"c\")\nSeq(E)\nCreateSeq(name:E,mml:\"d\")");
			assert((codes(r3) == std::vector<Code>{ Code::createSequenceDuplicateError }));	// 別名でも同じ名前空間
		}
		{// ' (位置を進めないモード)では Seq も位置を進めない
			const auto r = comp(head + "CreateSeq(name:X,mml:\"c\")\n' Seq(X) d");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o4c@0:480v100", "o4d@0:480v100" }));
		}
		{// Seq の直後の ^ は Seq 内の音符を延長しない(休符として位置だけ進む)
			const auto r = comp(head + "CreateSeq(name:X,mml:\"c\")\nc Seq(X)^ d");
			assert(!r.hasError());
			assert((notes(r, "a") == Strs{ "o4c@0:480v100", "o4c@480:480v100", "o4d@1440:480v100" }));	// Seq(X) は 480 進み、^ で更に 480
		}
		{// Seq の中で作った Port は同name,同instrument,同channel なら1つに結合され、instrument 省略時は呼び出し側のPortのものを引き継ぐ
			const auto r = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:n,channel:5) c\")\nSeq(X) Seq(X)");
			assert(!r.hasError());
			assert(portCount(r, "n") == 1);
			for (const auto& p : r.ports) if (p.name == "n") { assert(p.instrument == "fm"); assert(p.channel == 4); }
			assert((notes(r, "n") == Strs{ "o4c@0:480v100", "o4c@480:480v100" }));
		}
		{// 同名Portの重複 CreatePort はエラー。Seq の中で親と同名のPortを作り直す(隠蔽する)のは可能で、元Portの内容は親へ戻る
			const auto r1 = comp(head + "CreatePort(name:a,instrument:fm,channel:2)");
			assert((codes(r1) == std::vector<Code>{ Code::createPortDuplicateError }));

			const auto r2 = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:a,instrument:psg,channel:3) c\")\nc Seq(X) d");
			assert(!r2.hasError());
			assert(portCount(r2, "a") == 2);
			assert((notes(r2, "a") == Strs{ "o4c@0:480v100", "o4c@480:480v100", "o4d@960:480v100" }));
		}
		{// 隠蔽が孫の階層で起きても、元Portの内容は途中の階層を経由して祖先へ戻る
			const auto r = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:a,instrument:psg,channel:3) c\")\n"
				"CreateSeq(name:Y,mml:\"c Seq(X) d\")\nSeq(Y)");
			assert(!r.hasError());
			assert(portCount(r, "a") == 2);
			assert((notes(r, "a") == Strs{ "o4c@0:480v100", "o4c@480:480v100", "o4d@960:480v100" }));
		}
		{// Seq の中の中間イベント(V)は位置がずらされ、最終イベント(ControlChange)へ変換される
			const auto r = comp(head + "CreateSeq(name:X,mml:\"V(50) c\")\nr4 Seq(X)");
			assert(!r.hasError());
			bool found = false;
			for (const auto& p : r.ports) {
				for (const auto& ev : p.eventList) {
					const auto cc = dynamic_cast<const MmlCompiler::EventControlChange*>(ev.second.get());
					if (cc && ev.first == 480 && cc->no == static_cast<uint8_t>(midi::EventControlChange::Type::volume)) {
						assert(cc->value == 50);
						found = true;
					}
				}
			}
			assert(found);
		}
		{// 未定義Port: エラーは1件だけで、続く行は(クラッシュせず)現在のPortで処理される
			const auto r = comp(head + "Port(zzz)\nc d e\nPort(a) f\n");
			assert((codes(r) == std::vector<Code>{ Code::portNameError }));
		}
		{// Seq の呼び出しエラー: 未定義Sequence / 名前なし / length 指定誤り
			assert((codes(comp(head + "Seq(Q)")) == std::vector<Code>{ Code::sequenceNameError }));
			assert((codes(comp(head + "Seq()")) == std::vector<Code>{ Code::sequenceNameError }));
			const std::string def = head + "CreateSeq(name:X,mml:\"c\")\n";
			assert((codes(comp(def + "Seq(X,length:\"zz\")")) == std::vector<Code>{ Code::sequenceLengthError }));
			assert((codes(comp(def + "Seq(X,length:\"4 8\")")) == std::vector<Code>{ Code::sequenceLengthError }));
			assert((codes(comp(def + "Seq(X,length:4)")) == std::vector<Code>{ Code::sequenceLengthError }));
		}
		{// CreateSeq の誤り: 名前なし / mml なし / 重複
			assert((codes(comp(head + "CreateSeq(mml:\"c\")")) == std::vector<Code>{ Code::createSequenceNameError }));
			assert((codes(comp(head + "CreateSeq(name:X)")) == std::vector<Code>{ Code::createSequenceError }));
			assert((codes(comp(head + "CreateSeq(name:X,mml:\"c\")\nCreateSeq(name:X,mml:\"d\")")) == std::vector<Code>{ Code::createSequenceDuplicateError }));
		}
		{// Seq の中のエラーは(複数回呼ばれても)1件だけ報告される
			const auto r = comp(head + "CreateSeq(name:X,mml:\"c zzz\")\nSeq(X) Seq(X) c");
			assert((codes(r) == std::vector<Code>{ Code::unknownError }));
		}
		{// 出力される Port の順序は生成された順(Seq の中で生成された Port も、その生成時点の位置)
			// 子(main)で bass を生成した後、孫(baseE)で bassE を生成する -> bass, bassE の順
			const auto r1 = comp("CreateSeq(name:baseE, mml:\"\n CreatePort(name:bassE, channel:1) l8 v127 o1 e^<e>e^e<e^\n\")\n"
				"CreateSeq(name:main,mml:R\"main(\n CreatePort(name:bass, channel:1) l8 V(120) @33 o1 v127\n Seq(baseE)\n )main\")\n"
				"Seq(main)");
			assert(!r1.hasError());
			assert((names(r1) == Names{ "bass", "bassE" }));
			// ルートの a -> Seq 内の n -> ルートの b -> Seq 内の n(2回目は結合) の順
			const auto r2 = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:n,channel:5) c\")\nSeq(X) CreatePort(name:b,channel:2) Seq(X)");
			assert(!r2.hasError());
			assert((names(r2) == Names{ "a", "n", "b" }));
		}
		{// 同name,同instrument,同channel のPortの結合(SMFのTrackを増やさない)。イベントの処理順は書いた順を保つ
			// 例: main / baseE / baseA が全て bass を作る -> bass は1つ。@33 の後に e が並び、その後ろの小節に a が並ぶ
			{
				const auto r = comp(
					"CreateSeq(name:baseE, mml:\"\n CreatePort(name:bass, channel:1) l8 v127 o1 e^<e>e^e<e^\n\")\n"
					"CreateSeq(name:baseA, mml:\"\n CreatePort(name:bass, channel:1) l8 v127 o1 a^<a>a^a<a^\n\")\n"
					"CreateSeq(name:main,mml:R\"main(\n CreatePort(name:bass, channel:1) l8 V(120) @33 o1 v127\n Seq(baseE)\n Seq(baseA)\n )main\")\n"
					"CreatePort(name:root, instrument:fm, channel:2)\nSeq(main)");
				assert(!r.hasError());
				assert(portCount(r, "bass") == 1);
				assert((names(r) == Names{ "root", "bass" }));
				const auto e = evs(r, "bass");
				assert((Strs(e.begin(), e.begin() + 2) == Strs{ "C7=120@0", "P33@0" }));	// 先頭は V(120) と @33 の設定(書いた順)
				const auto n = notes(r, "bass");
				assert((n == Strs{ "o1e@0:480v127", "o2e@480:240v127", "o1e@720:480v127", "o1e@1200:240v127", "o2e@1440:480v127",
					"o1a@1920:480v127", "o2a@2400:240v127", "o1a@2640:480v127", "o1a@3120:240v127", "o2a@3360:480v127" }));	// baseE の次の小節に baseA
			}
			// 異なる instrument / channel / name は結合しない
			{
				const std::string def = "CreateSeq(name:X,mml:\"CreatePort(name:n,instrument:INS,channel:CH) c\")\n";
				auto mk = [&](const std::string& ins, const std::string& ch) {
					std::string d = def;
					d.replace(d.find("INS"), 3, ins); d.replace(d.find("CH"), 2, ch);
					return d;
				};
				assert(portCount(comp(head + mk("fm", "5") + "Seq(X) Seq(X)"), "n") == 1);
				const auto rI = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:n,instrument:fm,channel:5) c\")\n"
					"CreateSeq(name:Y,mml:\"CreatePort(name:n,instrument:psg,channel:5) c\")\nSeq(X) Seq(Y)");
				assert(!rI.hasError()); assert(portCount(rI, "n") == 2);
				const auto rC = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:n,instrument:fm,channel:5) c\")\n"
					"CreateSeq(name:Y,mml:\"CreatePort(name:n,instrument:fm,channel:6) c\")\nSeq(X) Seq(Y)");
				assert(!rC.hasError()); assert(portCount(rC, "n") == 2);
				const auto rN = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:n,instrument:fm,channel:5) c\")\n"
					"CreateSeq(name:Y,mml:\"CreatePort(name:m,instrument:fm,channel:5) c\")\nSeq(X) Seq(Y)");
				assert(!rN.hasError()); assert(portCount(rN, "n") == 1); assert(portCount(rN, "m") == 1);
				assert((names(rN) == Names{ "a", "n", "m" }));
			}
			// 同じ時刻の順序は書き込み順: ' (位置を進めない)での Seq(X) と @5
			{
				const std::string def = head + "CreateSeq(name:X,mml:\"c\")\n";
				const auto r1 = comp(def + "' Seq(X) @5");
				assert(!r1.hasError());
				const auto e1 = evs(r1, "a");
				assert((e1 == Strs{ "o4c@0:480v100", "P5@0" }));
				const auto r2 = comp(def + "' @5 Seq(X)");
				assert(!r2.hasError());
				assert((evs(r2, "a") == Strs{ "P5@0", "o4c@0:480v100" }));
			}
			// 同名Portを作る Seq 同士の順序は呼んだ順
			{
				const std::string def = head + "CreateSeq(name:X,mml:\"CreatePort(name:n,channel:5) c\")\n"
					"CreateSeq(name:Y,mml:\"CreatePort(name:n,channel:5) d\")\n";
				const auto r1 = comp(def + "' Seq(X) Seq(Y)");
				assert(!r1.hasError()); assert(portCount(r1, "n") == 1);
				assert((evs(r1, "n") == Strs{ "o4c@0:480v100", "o4d@0:480v100" }));
				const auto r2 = comp(def + "' Seq(Y) Seq(X)");
				assert((evs(r2, "n") == Strs{ "o4d@0:480v100", "o4c@0:480v100" }));
			}
			// Seq の後に同名を CreatePort した場合は、その Port が Seq の内容を引き継ぐ(生成順は最初のまま)
			{
				const auto r = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:n,channel:5) c\")\n"
					"Seq(X) CreatePort(name:n,channel:5) d CreatePort(name:z,channel:7) Seq(X)");
				assert(!r.hasError());
				assert(portCount(r, "n") == 1);
				assert((names(r) == Names{ "a", "n", "z" }));
				const auto n = notes(r, "n");
				assert((n == Strs{ "o4c@0:480v100", "o4d@0:480v100", "o4c@0:480v100" }));	// 書いた順(c の後に d、その後に2回目の c)
			}
			// 孫まで同名を作っても1つになる(ルートが先に bass を作っていても)
			{
				const auto r = comp("CreatePort(name:bass,instrument:fm,channel:1)\n"
					"CreateSeq(name:baseE,mml:\"CreatePort(name:bass,instrument:fm,channel:1) e\")\n"
					"CreateSeq(name:main,mml:\"CreatePort(name:bass,instrument:fm,channel:1) c Seq(baseE)\")\nSeq(main)");
				assert(!r.hasError());
				assert(portCount(r, "bass") == 1);
			}
			// 中間イベント(V)も書き込み順に変換される: ' Seq(X) V(100) は X の中の V(50) の後に 100
			{
				const auto r = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:a,instrument:fm,channel:1) V(50)\")\n' Seq(X) V(100)");
				assert(!r.hasError());
				assert(portCount(r, "a") == 1);
				const auto e = evs(r, "a");
				assert((e == Strs{ "C7=50@0", "C7=100@0" }));
			}
		}
		{// 中間イベントの変換は Port ごとに独立して行われ(各Portは既定値から開始)、その後に同じPortが結合される
			{// 同じ bass を作る Seq を結合しても、V(+=40) / V(-=40) は各Seq内の V(80) が基準になる
				const auto r = comp(
					"CreateSeq(name:baseE, mml:\"\n CreatePort(name:bass, channel:1) l8 o3 V(80) e V(+=40) e\n\")\n"
					"CreateSeq(name:baseA, mml:\"\n CreatePort(name:bass, channel:1) l8 o3 V(80) r r a V(-=40) a\n\")\n"
					"CreateSeq(name:main,mml:R\"main(\n CreatePort(name:bass, channel:1) l8 @1 'Seq(baseE) Seq(baseA)'\n)main\")\n"
					"Seq(main)");
				assert(!r.hasError());
				assert(portCount(r, "bass") == 1);
				assert((evs(r, "bass") == Strs{ "P1@0", "C7=80@0", "o3e@0:240v100", "C7=80@0", "C7=120@240", "o3e@240:240v100", "o3a@480:240v100", "C7=40@720", "o3a@720:240v100" }));
			}
			{// 変換を行わないまま結合しても、最終イベントに無い中間イベントは残らない(全イベントが最終イベントになっている)
				const auto r = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:n,channel:5) V(70) Pan(10) c\")\nSeq(X) Seq(X)");
				assert(!r.hasError());
				assert(portCount(r, "n") == 1);
				for (const auto& p : r.ports) for (const auto& ev : p.eventList) assert(!dynamic_cast<const MmlCompiler::Inner::InterEvent*>(ev.second.get()));
			}
			{// 親から引き継いだPortの中間イベントは親の状態の続きとして変換される(V(80) → +=10 → 90)
				const auto r = comp(head + "CreateSeq(name:X,mml:\"V(+=10)\")\nV(80) ' Seq(X) V(+=5)");
				assert(!r.hasError());
				assert((evs(r, "a") == Strs{ "C7=80@0", "C7=90@0", "C7=95@0" }));
			}
			{// ネストしても同様(孫の V(+=10) は祖先Portの状態に続く)
				const auto r = comp(head + "CreateSeq(name:Z,mml:\"V(+=10)\")\nCreateSeq(name:Y,mml:\"' Seq(Z) V(+=1)\")\nV(80) ' Seq(Y) V(+=5)");
				assert(!r.hasError());
				assert((evs(r, "a") == Strs{ "C7=80@0", "C7=90@0", "C7=91@0", "C7=96@0" }));
			}
			{// Seq の中で親と同名のPortを作り直して(隠蔽)も、元Portの中間イベントは親の状態に続く。新しいPortは既定値から
				const auto r = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:a,instrument:psg,channel:3) V(+=10)\")\nV(80) ' Seq(X) V(+=5)");
				assert(!r.hasError());
				assert(portCount(r, "a") == 2);
				for (const auto& p : r.ports) {
					if (p.name != "a") continue;
					std::vector<int> v;
					for (const auto& ev : p.eventList) if (const auto cc = dynamic_cast<const MmlCompiler::EventControlChange*>(ev.second.get())) v.push_back(cc->value);
					if (p.instrument == "psg") assert((v == std::vector<int>{ 110 }));
					else assert((v == std::vector<int>{ 80, 85 }));
				}
			}
			{// 同じSequenceを2回呼んで結合されても、各呼び出しは独立(既定値100 から +=10 で、どちらも110)
				const auto r = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:n,channel:5) V(+=10) c\")\nSeq(X) Seq(X)");
				assert(!r.hasError());
				assert(portCount(r, "n") == 1);
				assert((evs(r, "n") == Strs{ "C7=110@0", "o4c@0:480v100", "C7=110@480", "o4c@480:480v100" }));
			}
			{// 名前が違うPortは同じinstrument/channelでも独立(Port a の V(80) を b は引き継がない)
				const auto r = comp(head + "V(80)\nCreatePort(name:b,instrument:fm,channel:1) V(+=10)");
				assert(!r.hasError());
				assert((evs(r, "a") == Strs{ "C7=80@0" }));
				assert((evs(r, "b") == Strs{ "C7=110@0" }));
			}
			{// 変換前のSeq内エラーでは何も出力されない(従来どおりエラー)
				const auto r = comp(head + "CreateSeq(name:X,mml:\"V(50) zzz\")\nSeq(X)");
				assert((codes(r) == std::vector<Code>{ Code::unknownError }));
			}
		}
		{// 親のPortを Port(名前) で参照した後の、同名の CreatePort は createPortShadowError
			const auto r = comp("CreatePort(name:bass,channel:1) aaa\nCreateSeq(name:seqA,mml:\"Port(bass) aaa\nCreatePort(name:bass,channel:1) bbb\")\nSeq(seqA)");
			assert((codes(r) == std::vector<Code>{ Code::createPortShadowError }));
		}
		{// 参照した Port と別名の CreatePort は問題ない
			const auto r = comp("CreatePort(name:bass,channel:1) aaa\nCreateSeq(name:seqA,mml:\"Port(bass) aaa\nCreatePort(name:other,channel:2) bbb\")\nSeq(seqA)");
			assert(!r.hasError());
			assert(portCount(r, "bass") == 1 && portCount(r, "other") == 1);
		}
		{// 参照する前なら同名の CreatePort は可能(従来どおり。同name,同instrument,同channel は結合される)
			const auto r = comp("CreatePort(name:bass,channel:1) aaa\nCreateSeq(name:seqA,mml:\"CreatePort(name:bass,channel:1) bbb\nPort(bass) ccc\")\nSeq(seqA)");
			assert(!r.hasError());
			assert(portCount(r, "bass") == 1);
		}
		{// 参照したのが別のPortなら、その名前を CreatePort してもよい / 自分の階層で作ったPortの参照後の CreatePort は従来どおり重複エラー
			const auto r1 = comp("CreatePort(name:p,channel:1)\nCreatePort(name:q,channel:2)\nCreateSeq(name:S,mml:\"Port(p) a\nCreatePort(name:q,channel:3) b\")\nSeq(S)");
			assert(!r1.hasError());
			const auto r2 = comp("CreatePort(name:p,channel:1)\nCreatePort(name:x,channel:2)\nPort(p)\nCreatePort(name:p,channel:3)");
			assert((codes(r2) == std::vector<Code>{ Code::createPortDuplicateError }));
		}
		{// 孫の階層で Port(名前) した場合も、その階層での CreatePort が対象
			const auto r = comp("CreatePort(name:bass,channel:1)\nCreateSeq(name:Y,mml:\"Port(bass) a CreatePort(name:bass,channel:1) b\")\n"
				"CreateSeq(name:X,mml:\"Seq(Y)\")\nSeq(X)");
			assert((codes(r) == std::vector<Code>{ Code::createPortShadowError }));
			// 子で参照しても、孫は別スコープなので影響しない
			const auto r2 = comp("CreatePort(name:bass,channel:1)\nCreateSeq(name:Y,mml:\"CreatePort(name:bass,channel:1) b\")\n"
				"CreateSeq(name:X,mml:\"Port(bass) a Seq(Y)\")\nSeq(X)");
			assert(!r2.hasError());
		}
		{// Tempo は曲全体、MasterVolume は同じ instrument の全Portで値を引き継ぐ(時間順に処理される)
			// 指定Portの Tempo を <位置,BPM> で返す
			const auto tempos = [](const MmlCompiler::Result& r, std::string_view portName) {
				std::vector<std::pair<size_t, long>> v;
				for (const auto& p : r.ports) {
					if (p.name != portName) continue;
					for (const auto& ev : p.eventList) {
						const auto m = dynamic_cast<const MmlCompiler::EventMeta*>(ev.second.get());
						if (!m || m->data.size() != 3) continue;
						const auto usec = (m->data[0] << 16) | (m->data[1] << 8) | m->data[2];
						v.emplace_back(ev.first, std::lround(60000000.0 / usec));
					}
				}
				return v;
			};
			// 指定Portの MasterVolume(SysEx)を <位置,値> で返す
			const auto masterVolumes = [](const MmlCompiler::Result& r, std::string_view portName) {
				std::vector<std::pair<size_t, int>> v;
				for (const auto& p : r.ports) {
					if (p.name != portName) continue;
					for (const auto& ev : p.eventList) {
						const auto x = dynamic_cast<const MmlCompiler::EventSystemExclusive*>(ev.second.get());
						if (x && x->data.size() == 8) v.emplace_back(ev.first, (x->data[6] << 7) | x->data[5]);
					}
				}
				return v;
			};
			using TempoList = std::vector<std::pair<size_t, long>>;
			using VolList = std::vector<std::pair<size_t, int>>;
			{// 別Portの Tempo を引き継ぐ(b は a の t90 を基準に t-=20 = 70)
				const auto r = comp(head + "t90\nCreatePort(name:b,instrument:fm,channel:2) t-=20");
				assert(!r.hasError());
				assert((tempos(r, "a") == TempoList{ {0, 90} }));
				assert((tempos(r, "b") == TempoList{ {0, 70} }));
			}
			{// Port の並び順ではなく、位置(時間)の順に引き継ぐ(後から作った b の方が早い位置)
				const auto r = comp(head + "r1 t+=10\nCreatePort(name:b,instrument:fm,channel:2) t90");
				assert(!r.hasError());
				assert((tempos(r, "b") == TempoList{ {0, 90} }));
				assert((tempos(r, "a") == TempoList{ {1920, 100} }));
			}
			{// リタルダンド用の Seq を別Portから呼んでも、曲のその時点のテンポが基準になる
				const std::string def = head + "CreateSeq(name:rit,mml:\"t-=10 r1 t-=10\")\n"
					"t100 r1\nCreatePort(name:conductor,instrument:fm,channel:16) r1 Seq(rit)";
				const auto r = comp(def);
				assert(!r.hasError());
				assert((tempos(r, "a") == TempoList{ {0, 100} }));
				assert((tempos(r, "conductor") == TempoList{ {1920, 90}, {3840, 80} }));
			}
			{// 親のPortから Seq を呼んでも同じ(引き継いだPortの Tempo イベントも全体の時間順で処理される)
				const auto r = comp(head + "CreateSeq(name:rit,mml:\"t-=10 r1 t-=10\")\nt100 r1 Seq(rit) t-=5");
				assert(!r.hasError());
				assert((tempos(r, "a") == TempoList{ {0, 100}, {1920, 90}, {3840, 80}, {3840, 75} }));
			}
			{// 同じ位置に複数Portが相対指定を書くと、その分だけ積み上がる(Port の並び順)
				const auto r = comp(head + "t120\nCreatePort(name:b,instrument:fm,channel:2) t-=10\nCreatePort(name:c,instrument:fm,channel:3) t-=10");
				assert(!r.hasError());
				assert((tempos(r, "b") == TempoList{ {0, 110} }));
				assert((tempos(r, "c") == TempoList{ {0, 100} }));
			}
			{// MasterVolume は同じ instrument の Port で引き継ぎ、別 instrument は独立(既定値 16383 から)
				const auto r = comp(head + "MasterVolume(8000)\nCreatePort(name:b,instrument:fm,channel:2) MasterVolume(+=1000)\n"
					"CreatePort(name:c,instrument:psg,channel:1) MasterVolume(-=1000)");
				assert(!r.hasError());
				assert((masterVolumes(r, "a") == VolList{ {0, 8000} }));
				assert((masterVolumes(r, "b") == VolList{ {0, 9000} }));
				assert((masterVolumes(r, "c") == VolList{ {0, 15383} }));
			}
			{// MasterVolume も時間順: 後から作った Port の方が早い位置なら、そちらが先に作用する
				const auto r = comp(head + "r1 MasterVolume(+=500)\nCreatePort(name:b,instrument:fm,channel:2) MasterVolume(1000)");
				assert(!r.hasError());
				assert((masterVolumes(r, "b") == VolList{ {0, 1000} }));
				assert((masterVolumes(r, "a") == VolList{ {1920, 1500} }));
			}
			{// Channel 系(Volume)は従来どおり Port ごとに独立(Tempo だけが全体)
				const auto r = comp(head + "V(80)\nCreatePort(name:b,instrument:fm,channel:1) V(+=10)");
				assert(!r.hasError());
				for (const auto& p : r.ports) if (p.name == "b") {
					const auto cc = dynamic_cast<const MmlCompiler::EventControlChange*>(p.eventList.begin()->second.get());
					assert(cc && cc->value == 110);
				}
			}
			{// 変換後に中間イベントは残らない
				const auto r = comp(head + "CreateSeq(name:X,mml:\"t-=5 MasterVolume(+=1)\")\nt100 Seq(X) Seq(X)");
				assert(!r.hasError());
				for (const auto& p : r.ports) for (const auto& ev : p.eventList) assert(!dynamic_cast<const MmlCompiler::Inner::InterEvent*>(ev.second.get()));
			}
		}
		{// Seq の進む位置: 指定ナシは Seq 内の Position そのまま。CreateSeq の align で単位の倍数に切り上げ。Seq の length が最優先
			const auto pos = [&](const std::string& def, const std::string& call) {	// 呼んだ後に書いた e の位置
				const auto r = comp(head + def + "\n" + call + " e");
				assert(!r.hasError());
				const auto n = notes(r, "a");
				assert(!n.empty());
				const auto t = n.back();	// 最後の e
				assert(t.rfind("o4e@", 0) == 0);
				return std::stoul(t.substr(4));
			};
			// 指定ナシ: 書いた長さのまま(1小節に丸めない)。末尾の休符も数える
			assert(pos("CreateSeq(name:X,mml:\"c4 d4\")", "Seq(X)") == 960);
			assert(pos("CreateSeq(name:X,mml:\"c4 r2.\")", "Seq(X)") == 1920);
			assert(pos("CreateSeq(name:X,mml:\"c1 c1\")", "Seq(X)") == 3840);
			assert(pos("CreateSeq(name:X,mml:\"\")", "Seq(X)") == 0);
			// align:"1" (1小節単位): 切り上げ。ちょうど倍数ならそのまま
			assert(pos("CreateSeq(name:X,mml:\"c4 d4\",align:\"1\")", "Seq(X)") == 1920);
			assert(pos("CreateSeq(name:X,mml:\"c1\",align:\"1\")", "Seq(X)") == 1920);
			assert(pos("CreateSeq(name:X,mml:\"c1 c8\",align:\"1\")", "Seq(X)") == 3840);
			assert(pos("CreateSeq(name:X,mml:\"\",align:\"1\")", "Seq(X)") == 0);
			// align:"4" (4分音符単位) / "2." (付点2分音符=3/4拍子の1小節)
			assert(pos("CreateSeq(name:X,mml:\"c8\",align:\"4\")", "Seq(X)") == 480);
			assert(pos("CreateSeq(name:X,mml:\"c8 c8 c8\",align:\"4\")", "Seq(X)") == 960);
			assert(pos("CreateSeq(name:X,mml:\"c4\",align:\"2.\")", "Seq(X)") == 1440);
			assert(pos("CreateSeq(name:X,mml:\"c4 c2 c4\",align:\"2.\")", "Seq(X)") == 2880);
			// Seq の length 指定が最優先(align は無視される)
			assert(pos("CreateSeq(name:X,mml:\"c4 d4\",align:\"1\")", "Seq(X,length:\"4\")") == 480);
			// Seq の中で作った Port にだけ書く Seq でも、その長さだけ進む(別Portの位置も終了位置に含まれる)
			{
				const auto r = comp(head + "CreateSeq(name:X,mml:\"CreatePort(name:n,channel:5) c1\")\nSeq(X) Seq(X) e");
				assert(!r.hasError());
				assert((notes(r, "n") == Strs{ "o4c@0:1920v100", "o4c@1920:1920v100" }));
				assert((notes(r, "a") == Strs{ "o4e@3840:480v100" }));
			}
			// 孫の Seq の長さも積み上がる(X=960, Y=X+e4=1440)
			assert(pos("CreateSeq(name:X,mml:\"c4 d4\")\nCreateSeq(name:Y,mml:\"Seq(X) e4\")", "Seq(Y)") == 1440);
			// 親から引き継いだ同名Portを CreatePort で隠蔽しても、隠蔽された親のPortの位置が終了位置に含まれる(c4 c4 の 960)
			assert(pos("CreateSeq(name:X,mml:\"c4 c4 CreatePort(name:a,instrument:psg,channel:3) d4\")", "Seq(X)") == 960);
			// ' (位置を進めないモード)では align があっても進まない
			assert(pos("CreateSeq(name:X,mml:\"c4\",align:\"1\")", "' Seq(X)") == 0);
			// align の指定誤り(音長の書式でない / 0 / 余計な文字列 / 文字列でない)はエラー
			assert((codes(comp(head + "CreateSeq(name:X,mml:\"c\",align:\"zz\")")) == std::vector<Code>{ Code::createSequenceError }));
			assert((codes(comp(head + "CreateSeq(name:X,mml:\"c\",align:\"4 8\")")) == std::vector<Code>{ Code::createSequenceError }));
			assert((codes(comp(head + "CreateSeq(name:X,mml:\"c\",align:4)")) == std::vector<Code>{ Code::createSequenceError }));
			assert((codes(comp(head + "CreateSeq(name:X,mml:\"c\",align:\"0\")")) == std::vector<Code>{ Code::lengthError }));
			// 呼ばれない Seq の align の誤りも定義の時点で検出される
			assert(comp(head + "CreateSeq(name:X,mml:\"c\",align:\"zz\")\nc").hasError());
		}
		{// 仕様: 呼ばれないSequenceの中身は(呼び出し側の状態に依存するため)検証されない
			const auto r = comp(head + "CreateSeq(name:X,mml:\"zzz\")\nc");
			assert(!r.hasError());
		}
	}

	{// compile(): 音長指定の異常値(0分音符はゼロ除算にならず lengthError。巨大値も lengthError)
		std::cout << "compile (length)" << std::endl;
		const std::string head = "CreatePort(name:a,instrument:fm,channel:1)\n";
		for (const char* mml : { "c0", "l0", "r0", "c2147483648", "c!2147483648", "c0.", "c4+0" }) {
			const auto r = MmlCompiler::compile(head + mml);
			assert(r.hasError());
			assert(r.errors[0].code == MmlCompiler::ErrorCode::lengthError);
		}
		for (const char* mml : { "c1920", "c!0", "c!2147483647", "c4.", "c4...", "c4+8-16", "l8 c" }) {	// 正常系(リグレッション確認)
			const auto r = MmlCompiler::compile(head + mml);
			assert(!r.hasError());
		}
	}

	{// 全ての ErrorCode にメッセージが定義されている
		std::cout << "getMessage (all codes)" << std::endl;
		for (int i = static_cast<int>(MmlCompiler::ErrorCode::lengthError); i <= static_cast<int>(MmlCompiler::ErrorCode::stdEexceptionError); i++) {
			assert(MmlCompiler::Result::getMessage(static_cast<MmlCompiler::ErrorCode>(i)) != "unknown");
		}
	}

}
