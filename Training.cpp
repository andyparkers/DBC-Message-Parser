#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <tuple>
#include <vector>


enum Encoding {
    BIG_ENDIAN = 0,
    LITTLE_ENDIAN = 1
};

struct CANSignal {
    Encoding encoding_type = BIG_ENDIAN;
    std::string name = "";
    std::string unit = "";
    double scale_factor = 0.0;
    double offset = 0.0;
    int start_bit = 0;
    int bit_length = 0;
    bool is_signed = 0;
};

struct CANMessage {
    std::vector<CANSignal> signals;
    std::string name;
    int id;
};

std::ostream& operator<<(std::ostream& out, const CANSignal& data) {
    out << "Name: " << data.name << ", unit: " << data.unit << ", scale: " << data.scale_factor
        << ", offset: " << data.offset << ", start bit: " << data.start_bit << ", bit length: " << data.bit_length
        << ", signed: " << data.is_signed;
    return out;
}

std::ostream& operator<<(std::ostream& out, const CANMessage& data) {
    out << "Name: " << data.name << ", ID: " << data.id << std::endl;
    for (const auto& row : data.signals) {
        out << row << std::endl;
    }
    return out;
}

std::pair<std::string, int> ParseMessage(const std::string& input_data) {
    return std::make_pair(std::string(input_data.begin() + input_data.find(" ", 5) + 1,
        input_data.begin() + input_data.find_first_of(":", 0)),
        std::stoi(input_data.substr(4, input_data.find(" ", 5)))
    );
}

std::vector<std::string> SplitIntoWords(const std::string& text) {
    std::vector<std::string> words;
    words.reserve(text.length() / 5);
    std::string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(std::move(word));
            }
        }
        else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(std::move(word));
    }

    return words;
}

std::pair<double, double> ParseScaleOffset(const std::string& data) {
    double scale = std::stof(data.substr(1, data.find(",", 0)));
    double offset = std::stof(data.substr(data.find(",", 0) + 1, data.size() - 2));
    return std::make_pair(scale, offset);
}

std::tuple<int, int, bool, Encoding> ParseLenghtStartBit(const std::string& data) {
    int start_bit = std::stoi(data.substr(0, data.find("|", 0)));
    int length = std::stoi(data.substr(data.find("|", 0) + 1, data.find("@", 0)));
    bool is_signed = data.find("-", 0) != std::string::npos ? 1 : 0;
    Encoding encoding_type = data.find("@0", 0) == std::string::npos ? BIG_ENDIAN : LITTLE_ENDIAN;
    return std::tie(length, start_bit, is_signed, encoding_type);
}

std::string ParseUnit(const std::string& data) {
    return std::string(data.begin() + 1, data.end() - 1);
}

std::string ParseName(const std::string& data) {
    return std::string(data.begin() + 5, data.begin() + data.find_first_of(" ", 5));
}

int ExtractCANId(const std::string& data) {
    if (data.find("BO_") == std::string::npos) {
        return -1;
    }
    size_t lhs_space_pos = data.find(" ", 0);
    size_t rhs_space_pos = data.find(" ", lhs_space_pos + 1);
    int can_id = std::stoi(data.substr(lhs_space_pos, rhs_space_pos));
    return can_id;
}

std::string TrimString(const std::string& s) {
    size_t start_pos = s.find_first_not_of(" \t");
    if (start_pos == std::string::npos) {
        return "";
    }
    size_t end_pos = s.find_last_not_of(" \t");
    return s.substr(start_pos, end_pos - start_pos + 1);
}

CANSignal ExtractRulesFromSingleSignal(const std::string& input_data) {
    std::string str_data = TrimString(input_data);
    CANSignal result;
    result.name = ParseName(str_data);
    size_t start_pos = str_data.find(": ");
    std::vector<std::string> words = SplitIntoWords(str_data);
    for (const auto& word : words) {
        if (word.find("@") != std::string::npos) {
            auto parsed_data = ParseLenghtStartBit(word);
            result.bit_length = std::get<0>(parsed_data);
            result.start_bit = std::get<1>(parsed_data);
            result.is_signed = std::get<2>(parsed_data);
            result.encoding_type = std::get<3>(parsed_data);
        }
        else if (word.find("(") != std::string::npos) {
            std::pair<double, double> parsed_data = ParseScaleOffset(word);
            result.scale_factor = parsed_data.first;
            result.offset = parsed_data.second;
        }
        else if (word.find("\"") != std::string::npos) {
            result.unit = ParseUnit(word);
        }
    }
    return result;
}

double ComputeValue(uint64_t raw_data, const std::string& input_data) {
    CANSignal signal = ExtractRulesFromSingleSignal(input_data);
    int start_bit = signal.start_bit;
    int length = signal.bit_length;
    uint64_t match_value = 0;
    for (int i = 0; i < length; ++i) {
        match_value |= (static_cast<unsigned long long>(1) << (start_bit + i));
    }
    match_value = (match_value & raw_data) >> start_bit;
    double result = match_value * signal.scale_factor + signal.offset;          // physical value = scale * (parsed data) + offset
    return result;
}


int main() {
    std::ifstream dbcFile("FN1_solar_dbc.dbc");

    if (!dbcFile.is_open()) {
        std::cerr << "File can not be opened." << std::endl;
        return 1;
    }

    std::vector<CANMessage> messages;

    std::string line;
    while (std::getline(dbcFile, line)) {
        if (line.size() > 3 && std::string(line.begin(), line.begin() + 4) == "BO_ ") {
            auto parsed_data = ParseMessage(line);
            CANMessage insert_data;
            insert_data.id = parsed_data.second;
            insert_data.name = parsed_data.first;
            line.clear();
            while (std::getline(dbcFile, line)) {
                if (line.empty()) {
                    std::cout << insert_data << std::endl;
                    messages.push_back(insert_data);
                    break;
                }
                else {
                    insert_data.signals.push_back(ExtractRulesFromSingleSignal(line));
                }
            }
        }
        line.clear();
    }
     
    for (const auto& message : messages) {
        std::cout << message << std::endl;
    }

    dbcFile.close();

    return 0;
}
