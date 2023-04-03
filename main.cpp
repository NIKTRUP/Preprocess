#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace std;
using filesystem::path;

// #include "..."
// В этом случае поиск файла выполняется относительно текущего файла, где расположена сама директива.
// Если файл не найден, поиск выполняется последовательно по всем элементам вектора include_directories.
static regex include1(R"(\s*#\s*include\s*\"([^"]*)\"\s*)");
// #include <...>
// Поиск выполняется последовательно по всем элементам вектора include_directories.
static regex include2(R"(\s*#\s*include\s*<([^>]*)>\s*)");

static bool NotFound(const path& matched_path, const path& file, std::uint64_t line_number, std::ostream& out = std::cout){
    out << "unknown include file " << matched_path.string() << " at file " << file.string() << " at line " << line_number << endl;
    return false;
}

path operator""_p(const char* data, std::size_t sz) {
    return path(data, data + sz);
}

bool IncludeParsing(std::ifstream& in, std::ofstream& out, const path& file, const vector<path>& include_directories);

bool FileIsFound(std::ofstream& out, const path& desired_path, const vector<path>& include_directories){
    std::ifstream in;
    while (!in.is_open()) {
        in.open(desired_path, std::ios::in | std::ios::binary);
    }
    return IncludeParsing(in, out, desired_path, include_directories);
}

bool FindFileInCurrentDirectory(std::ofstream& out, const path& matched_path, const path& parent_path, const vector<path>& include_directories){
    path desired_path = (parent_path / matched_path);
    for (const auto& current : filesystem::recursive_directory_iterator(parent_path)) {
        if (current.path() == desired_path) {
            return FileIsFound(out, desired_path, include_directories);
        }
    }
    return false;
}

bool FindFileInIncludeDirectories(std::ofstream& out, const path& matched_path, const vector<path>& include_directories){
    for (const auto& dir : include_directories) {
        if (exists(dir)) {
            path desired_path = (dir / matched_path);
            for (const auto& file : filesystem::recursive_directory_iterator(dir)) {
                if (file.path() == desired_path) {
                    return FileIsFound(out, desired_path, include_directories);
                }
            }
        }
    }
    return false;
}

bool IncludeParsing(std::ifstream& in, std::ofstream& out, const path& file, const vector<path>& include_directories){
    std::string line;
    std::uint64_t line_number = 0;

    while(std::getline(in, line)){
        ++line_number;
        smatch m;
        if (regex_match(line, m, include1)){
            auto dir = file.parent_path();
            path matched_path = string(m[1]);
            bool found = FindFileInCurrentDirectory(out, matched_path, dir, include_directories);
            if(!found){
                found = FindFileInIncludeDirectories(out, matched_path, include_directories);
            }
            if(!found){
                 return NotFound(matched_path, file, line_number, std::cout);
            }
        }else if(regex_match(line, m, include2)){
             path matched_path = string(m[1]);
             if(!FindFileInIncludeDirectories(out, matched_path, include_directories)){
                 return NotFound(matched_path, file, line_number, std::cout);
             }
        }else{
            out << line << '\n';
        }
    }
    return true;
}

// напишите эту функцию
bool Preprocess(const path& in_file, const path& out_file, const vector<path>& include_directories){
    std::ifstream in(in_file, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ofstream out(out_file, std::ios::out | std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    return IncludeParsing(in, out, in_file, include_directories);
}

string GetFileContents(string file) {
    ifstream stream(file);

    // конструируем string по двум итераторам
    return {(istreambuf_iterator<char>(stream)), istreambuf_iterator<char>()};
}

void Test() {
    error_code err;
    filesystem::remove_all("sources"_p, err);
    filesystem::create_directories("sources"_p / "include2"_p / "lib"_p, err);
    filesystem::create_directories("sources"_p / "include1"_p, err);
    filesystem::create_directories("sources"_p / "dir1"_p / "subdir"_p, err);

    {
        ofstream file("sources/a.cpp");
        file << "// this comment before include\n"
                "#include \"dir1/b.h\"\n"
                "// text between b.h and c.h\n"
                "#include \"dir1/d.h\"\n"
                "\n"
                "int SayHello() {\n"
                "    cout << \"hello, world!\" << endl;\n"
                "#   include<dummy.txt>\n"
                "}\n"sv;
    }
    {
        ofstream file("sources/dir1/b.h");
        file << "// text from b.h before include\n"
                "#include \"subdir/c.h\"\n"
                "// text from b.h after include"sv;
    }
    {
        ofstream file("sources/dir1/subdir/c.h");
        file << "// text from c.h before include\n"
                "#include <std1.h>\n"
                "// text from c.h after include\n"sv;
    }
    {
        ofstream file("sources/dir1/d.h");
        file << "// text from d.h before include\n"
                "#include \"lib/std2.h\"\n"
                "// text from d.h after include\n"sv;
    }
    {
        ofstream file("sources/include1/std1.h");
        file << "// std1\n"sv;
    }
    {
        ofstream file("sources/include2/lib/std2.h");
        file << "// std2\n"sv;
    }

    assert((!Preprocess("sources"_p / "a.cpp"_p, "sources"_p / "a.in"_p,
                                  {"sources"_p / "include1"_p,"sources"_p / "include2"_p})));

    ostringstream test_out;
    test_out << "// this comment before include\n"
                "// text from b.h before include\n"
                "// text from c.h before include\n"
                "// std1\n"
                "// text from c.h after include\n"
                "// text from b.h after include\n"
                "// text between b.h and c.h\n"
                "// text from d.h before include\n"
                "// std2\n"
                "// text from d.h after include\n"
                "\n"
                "int SayHello() {\n"
                "    cout << \"hello, world!\" << endl;\n"sv;

    assert(GetFileContents("sources/a.in"s) == test_out.str());
}

int main() {
    Test();
}
