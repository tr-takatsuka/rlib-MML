// rlib-Json https://github.com/tr-takatsuka/rlib-StringFormat
// This software is released under the CC0.
/*
boost::format を利用した文字列フォーマットです。C++17 での実装です。

+ boost::format を利用した printf 風に使える文字列フォーマットです。
  + % で繋げるのではなく、可変長引数の関数として使えます。
+ boost::format の特徴を活かし、型安全で、例外発生のないように実装しています。
  + 不正な型（非対応の型）を引数に与えるとコンパイルエラーになります。
+ stlコンテナ等を引数に指定出来ます。展開して出力します。
  + std::pair std::tuple
    std::unique_ptr std::shared_ptr std::optional
    std::array std::vector std::list std::deque std::initializer_list
    std::set std::multiset std::map std::multimap

・使い方例
	using std::cout;
	using std::endl;
	using namespace rlib::string;

	// 以下コードは printf 風の書式しか使っておりませんが、中身は boost::format です。
	// 書式の詳細はそちらのリファレンスもご参照下さい。

	{// 普通の使い方
		cout << format(u8"%s %dSX SR%dDE%s", u8"日産", 180, 20, "T") << endl;	// 日産 180SX SR20DET
	}
	{// std::wstring も可です
		std::wstring r = format(L"BNR%d RB%dDE%s", 32, 26, L"TT");	// L"BNR32 RB26DETT"
		std::wcout << r << endl;	// L"BNR32 RB26DETT"
	}
	{// 適してない型でも例外発生しません
		cout << format("%dSX %s", 240, L"日産") << endl;	// 240SX 00007FF742CC123C(例)
	}
	{// std::string をそのまま書けます。c_str() が不要なので便利。
		const std::string a = u8"トヨタ";
		std::string b = "1JZ-GTE";
		cout << format("%s jzx90 %s", a, b) << endl;	// トヨタ jzx90 1JZ-GTE
	}
#ifdef _MSC_VER
	{// VisualStudio の CString
		CString t0(_T("マツダ"));
		const CString t1(_T("13B-T"));
		auto r = format(_T("%s FC3S %s"), t0, t1);	// マツダ FC3S 13B-T
	}
#endif
	{// std::tuple std::pair は展開します。
		std::tuple<std::string, int, double> a{ "RPS13",1998,11.8 };
		const std::pair<std::string, int> b{ "FR", 1848000 };
		cout << format(			// 形式:RPS13 排気量:1998cc 燃費:11.8km/l 駆動方式:FR 価格:1848000円
			u8"形式:%s 排気量:%dcc 燃費:%.1fkm/l 駆動方式:%s 価格:%d円", a, b) << endl;
	}
	{// std::unique_ptr std::shared_ptr std::optional は中身を出力にします。空(無効)の場合は"(null)"を出力します
		std::unique_ptr<int> u = std::make_unique<int>(1);
		std::shared_ptr<int> s = std::make_shared<int>(2);
		std::optional<int> o(3);
		std::optional<int> e;
		cout << format("%d,%d,%d,%d", u, s, o, e) << endl;	// 1,2,3,(null)
	}
	{// その他コンテナも展開します。(やりすぎかもしれません。素直にコンパイルエラーに振ってもよいかも。)
		cout << format("%d,%d,%d", std::array<int, 3>{ 1, 2, 3 }) << endl;			// 1,2,3
		cout << format("%d,%d,%d", std::vector<int>{4, 5, 6}) << endl;				// 4,5,6
		cout << format("%d,%d,%d", std::list<int>{7, 8, 9}) << endl;				// 7,8,9
		cout << format("%d,%d,%d", std::deque<int>{10, 11, 12}) << endl;			// 10,11,12
		cout << format("%d,%d,%d", std::set<int>{3, 2, 1}) << endl;					// 1,2,3 (取れる順で出力します)
		cout << format("%d,%d,%d", std::map<int, int>{ {1, 2}, { 3,4 }}) << endl;	// 1,2,3
		cout << format("%d,%d,%d", std::multiset<int>{1, 2, 3}) << endl;			// 1,2,3"
		cout << format("%d,%d,%d", std::multimap<int, int>{ {1, 2}, { 3,4 }}) << endl;// 1,2,3
	}
	{// コンテナの中も再帰で展開するので以下みたいな型も可です
		std::map<int, std::pair<std::string, std::vector<int>>> m{
			{1, {"a", {5,6,7}}},
			{8, {"b", {9,10}}}
		};
		cout << format("%s,%s,%s,%s,%s,%s", m) << endl;	// 1,a,5,6,7,8
	}
	{// boost::format なので未対応の型はコンパイルエラーになってくれます
		std::function<void()> f;
//		cout << format("%s", f) << endl;	// コンパイルエラー
	}
*/
#pragma once

#include <boost/format.hpp>

namespace rlib
{
	namespace string
	{
		namespace inner
		{
			template<class> struct IsArray :std::false_type {};
			template<class T, std::size_t N> struct IsArray<std::array<T, N>> :std::true_type {};

			template<class> struct IsVector :std::false_type {};
			template<class T1, class T2> struct IsVector<std::vector<T1, T2>> :std::true_type {};

			template<class> struct IsList :std::false_type {};
			template<class T1, class T2> struct IsList<std::list<T1, T2>> :std::true_type {};

			template<class> struct IsDeque :std::false_type {};
			template<class T1, class T2> struct IsDeque<std::deque<T1, T2>> :std::true_type {};

			template<class> struct IsInitializerList :std::false_type {};
			template<class T> struct IsList<std::initializer_list<T>> :std::true_type {};

			template<class> struct IsSet :std::false_type {};
			template<class T1, class T2, class T3> struct IsSet<std::set<T1, T2, T3>> :std::true_type {};

			template<class> struct IsMultiSet :std::false_type {};
			template<class T1, class T2, class T3> struct IsMultiSet<std::multiset<T1, T2, T3>> :std::true_type {};

			template<class> struct IsMap :std::false_type {};
			template<class T1, class T2, class T3, class T4> struct IsMap<std::map<T1, T2, T3, T4>> :std::true_type {};

			template<class> struct IsMultiMap :std::false_type {};
			template<class T1, class T2, class T3, class T4> struct IsMultiMap<std::multimap<T1, T2, T3, T4>> :std::true_type {};

			template<class> struct IsPair :std::false_type { };
			template<class T1, class T2> struct IsPair<std::pair<T1,T2>> :std::true_type {};

			template<class> struct IsUniquePtr :std::false_type {};
			template<class T1, class T2> struct IsUniquePtr<std::unique_ptr<T1,T2>> :std::true_type {};

			template<class> struct IsSharedPtr :std::false_type {};
			template<class T> struct IsSharedPtr<std::shared_ptr<T>> :std::true_type {};

			template<class> struct IsOptional :std::false_type {};
			template<class T> struct IsOptional<std::optional<T>> :std::true_type {};

			template<class> struct IsTuple :std::false_type {};
			template<class... T> struct IsTuple<std::tuple<T...>> :std::true_type {};

			template <class CharT, class Head, class... Tail> void f(boost::basic_format<CharT>&, Head&, Tail&&...);
			template <class CharT> void f(const boost::basic_format<CharT>&) {}

			template <std::size_t I, class CharT, class... T, class... Tail> void f(boost::basic_format<CharT>& format, const std::tuple<T...>& head, Tail&&... tail) {
				if constexpr (I < sizeof...(T)) {
					f<I + 1, CharT>(format % std::get<I>(head), head, tail...);
				} else {
					f<CharT>(format, tail...);
				}
			}

			template <class CharT, class Head, class... Tail> void f(boost::basic_format<CharT>& format, Head& head, Tail&&... tail) {
				using HeadT = std::remove_const_t<Head>;
				if constexpr (IsUniquePtr<HeadT>::value || IsSharedPtr<HeadT>::value || IsOptional<HeadT>::value) {
					if (head) {
						f<CharT>(format, *head, tail...);
					} else {
						f<CharT>(format % "(null)", tail...);
					}
				} else if constexpr (IsPair<HeadT>::value) {
					f<CharT>(format, head.first, head.second, tail...);
				} else if constexpr (IsTuple<HeadT>::value) {
					f<0, CharT>(format, head, tail...);
				} else if constexpr (IsArray<HeadT>::value || IsVector<HeadT>::value || IsList<HeadT>::value || IsDeque<HeadT>::value
					|| IsInitializerList<HeadT>::value || IsSet<HeadT>::value || IsMultiSet<HeadT>::value
					|| IsMap<HeadT>::value || IsMultiMap<HeadT>::value)		// 範囲ループ可の型か？っていう便利記述がもしあるなら知りたい
				{
					for (const auto& i : head) {
						f<CharT>(format, i);
					}
					f<CharT>(format, tail...);
#ifdef _MSC_VER
				} else if constexpr (std::is_same_v<HeadT, CStringA> || std::is_same_v<HeadT, CStringW>) {
					f<CharT>(format % std::basic_string<CharT>(head.GetString()), tail...);
#endif
				} else {
					f<CharT>(format % head, tail...);
				}
			}
		}

		template <class CharT, class... Args> std::basic_string<CharT> format(const CharT* lpszFormat, Args&&... args) {
			boost::basic_format<CharT> format;
			format.exceptions(boost::io::no_error_bits);	// 例外を発生させない
			format.parse(lpszFormat);
			try {
				inner::f<CharT>(format, args...);
			} catch (...) {
			}
			return format.str();
		}
		template <class CharT, class... Args> std::basic_string<CharT> format(const std::basic_string<CharT>& s, Args&&... args) {
			return format<CharT>(s.c_str(), args...);
		}
	}
}


