#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <conio.h>
#include <regex>

constexpr size_t CHUNK = 1 << 20;        // 1MB chunk
constexpr size_t OVERLAP = 100;          // overlap size for streaming (if needed)

bool isValidUtf8(const std::string& str) {
    int cnt = 0;
    for (unsigned char c : str) {
        if (cnt == 0) {
            if ((c >> 5) == 0x6) cnt = 1;
            else if ((c >> 4) == 0xE) cnt = 2;
            else if ((c >> 3) == 0x1E) cnt = 3;
            else if ((c >> 7)) return false;
        }
        else {
            if ((c >> 6) != 0x2) return false;
            --cnt;
        }
    }
    return cnt == 0;
}

bool looksLikeUtf16LE(const std::string& snippet) {
    if (snippet.size() < 4 || snippet.size() % 2 != 0) return false;
    int zeroCount = 0;
    for (size_t i = 1; i < snippet.size(); i += 2) if (snippet[i] == 0) ++zeroCount;
    return (double)zeroCount / (snippet.size() / 2) > 0.3;
}

void printUtf16(const std::string& raw) {
    size_t wc = raw.size() / 2;
    const wchar_t* wdata = reinterpret_cast<const wchar_t*>(raw.data());
    int len = WideCharToMultiByte(CP_UTF8, 0, wdata, (int)wc, nullptr, 0, nullptr, nullptr);
    if (len <= 0) { std::cerr << "[Error] UTF-16→UTF-8 failed\n"; return; }
    std::vector<char> out(len);
    WideCharToMultiByte(CP_UTF8, 0, wdata, (int)wc, out.data(), len, nullptr, nullptr);
    std::cout.write(out.data(), len).put('\n');
}

void printUtf8(const std::string& s) {
    if (isValidUtf8(s)) std::cout << s << '\n';
    else std::cout << "[Invalid UTF-8]\n";
}

std::string BrowseForFile(const char* filter = "All Files\0*.*\0") {
    OPENFILENAMEA ofn = { sizeof(ofn) };
    char fn[MAX_PATH] = {};
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fn;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameA(&ofn) ? std::string(fn) : std::string();
}

void showProgressBar(size_t current, size_t total) {
    int width = 50;
    int percent = total ? static_cast<int>((double)current / total * 100) : 100;
    int filled = percent * width / 100;
    std::cout << '\r' << '[';
    for (int i = 0; i < width; ++i) std::cout << (i < filled ? '=' : ' ');
    std::cout << "] " << percent << "%" << std::flush;
}

// Fast keyword search (option 1)
bool searchKeywordInBinary(const std::string& filepath, const std::string& kw) {
    std::ifstream f(filepath, std::ios::binary | std::ios::ate);
    if (!f) { std::cerr << "[Error] Cannot open " << filepath << "\n"; return false; }
    size_t total = f.tellg(); f.seekg(0);
    std::ofstream log("search_log.txt", std::ios::app);
    log << "\n* '" << kw << "' in " << filepath << " *\n";

    std::vector<char> buffer(total);
    size_t read = 0;
    // Read whole file for progress
    while (read < total) {
        size_t toRead = std::min(CHUNK, total - read);
        f.read(buffer.data() + read, toRead);
        read += f.gcount();
        showProgressBar(read, total);
    }
    std::cout << '\n';
    std::string content(buffer.begin(), buffer.end());

    size_t count = 0;
    size_t pos = 0;
    while ((pos = content.find(kw, pos)) != std::string::npos) {
        std::cout << ++count << ") @" << pos << "\n";
        log << count << ": @" << pos << "\n";
        pos += kw.size();
    }
    if (count == 0) std::cout << "No matches found\n";
    return true;
}

// Regex-based full-file search (options 2 & 3)
void searchByRegexFile(const std::string& filepath, const std::regex& pat, bool isContact) {
    std::ifstream f(filepath, std::ios::binary | std::ios::ate);
    if (!f) { std::cerr << "[Error] Cannot open " << filepath << "\n"; return; }
    size_t total = f.tellg(); f.seekg(0);
    std::cout << "[Press 's' to stop] Reading file..." << std::endl;

    std::vector<char> buffer(total);
    size_t read = 0;
    while (read < total) {
        size_t toRead = std::min(CHUNK, total - read);
        f.read(buffer.data() + read, toRead);
        read += f.gcount();
        showProgressBar(read, total);
        if (_kbhit() && _getch() == 's') { std::cout << "\n[Stopped]\n"; return; }
    }
    std::cout << '\n';
    std::string content(buffer.begin(), buffer.end());

    size_t count = 0;
    for (auto it = std::sregex_iterator(content.begin(), content.end(), pat);
        it != std::sregex_iterator(); ++it) {
        if (_kbhit() && _getch() == 's') { std::cout << "\n[Stopped]\n"; return; }
        auto m = *it;
        size_t pos = m.position();
        std::cout << ++count << ") ";
        if (isContact && m.size() >= 3) {
            std::cout << m.str(1) << ": " << m.str(2);
        }
        else {
            std::cout << m.str();
        }
        std::cout << " @" << pos << "\n";
    }
    if (count == 0) std::cout << "No matches found\n";
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    while (true) {
        std::cout << "\n=== 기능 선택 ===\n";
        std::cout << "1. 검색어 직접 입력\n";
        std::cout << "2. 연락처 추출\n";
        std::cout << "3. 이메일 추출\n";
        std::cout << "4. 종료\n> ";
        std::string choice;
        std::getline(std::cin, choice);

        if (choice == "1") {
            std::cout << "키워드: ";
            std::string kw;
            std::getline(std::cin, kw);
            if (!kw.empty()) {
                auto fp = BrowseForFile();
                if (!fp.empty()) searchKeywordInBinary(fp, kw);
            }
        }
        else if (choice == "2") {
            auto fp = BrowseForFile();
            if (!fp.empty()) {
                std::regex rx(R"(([가-힣]{3,8})[ \t]*?(01[016789][-]?[0-9]{3,4}[-]?[0-9]{4}))");
                searchByRegexFile(fp, rx, true);
            }
        }
        else if (choice == "3") {
            auto fp = BrowseForFile();
            if (!fp.empty()) {
                std::regex rx(R"((\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b))");
                searchByRegexFile(fp, rx, false);
            }
        }
        else if (choice == "4") {
            break;
        }
        else {
            std::cout << "잘못된 입력\n";
        }
    }
    return 0;
}