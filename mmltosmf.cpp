
#include <bits/stdc++.h>
#include <boost/program_options.hpp>

#include "./sequencer/MmlToSmf.h"

using namespace rlib;
using namespace rlib::sequencer;

int main(const int argc, const char* const argv[])
{
	namespace po = boost::program_options;

	try {
		std::string input = "-", output = "-";
		std::string format = "text";
		po::options_description desc("options");
		desc.add_options()
			("version", "show version")
			("help", "show help")
			("format", po::value(&format)
				->default_value("text")
				->notifier([](const std::string& f) {
					if (f != "text" && f != "json") throw po::validation_error(po::validation_error::invalid_option_value, "format", f);
				}),
				"output format: text (default) or json")
			("input,i", po::value(&input), "input file (mml)")		// 入力ファイルパス(mml)
			("output,o", po::value(&output), "output file (mid)")	// 出力ファイル(mid)
			;

		po::positional_options_description pd;
		// pd.add("input", -1);

		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).positional(pd).run(), vm);

		if (vm.count("version")) {
			std::cout << "mmltosmf version 1.2.8" << std::endl;
			return 0;
		}

		if (vm.count("help")) {
			std::cout << desc << std::endl;		// ヘルプ表示
			return 0;
		}

		po::notify(vm);

		const std::string mml = [&] {
			if (input != "-") {
				auto path = std::filesystem::path(input);
				std::ifstream fs(path, std::ios::in | std::ios::binary);
				if (fs.fail()) throw std::runtime_error("input file open error.");
				const std::istreambuf_iterator<char> begin(fs);
				return std::string(begin, std::istreambuf_iterator<char>());
			}
			return std::string(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());	// stdin
		}();

		const auto r = mmlToSmf(mml);
		if (r.hasError()) {
			const auto err = format == "json" ? r.mmlResult.getJson(r.mmlResult.errors) : r.mmlResult.getText(r.mmlResult.errors);
			std::cerr << err << std::endl;
			return 1;
		}
		const auto fileImage = r.smf.getFileImage();
		if (output != "-") {
			auto path = std::filesystem::path(output);
			std::ofstream fs(path, std::ios::out | std::ios::binary | std::ios::trunc);
			if (fs.fail()) throw std::runtime_error("output file open error.");
			fs.write(reinterpret_cast<const char*>(fileImage.data()), fileImage.size());
		} else {
			std::cout.write(reinterpret_cast<const char*>(fileImage.data()), fileImage.size());	// stdout
		}

	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
		return 1;
	} catch (...) {
		std::cerr << "unknown exception" << std::endl;
		return 1;
	}

	return 0;
}
