#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>

// Generic CSV row - just stores raw line as string
class CSVRow {
public:
    CSVRow(const std::string& line) : raw_line_(line) {}
    
    std::string GetRaw() const { return raw_line_; }
    
    // Parse specific fields if needed (0-indexed)
    std::string GetField(size_t index, char delimiter = ',') const {
        std::stringstream ss(raw_line_);
        std::string field;
        size_t current_idx = 0;
        
        while (std::getline(ss, field, delimiter)) {
            if (current_idx == index) {
                return field;
            }
            current_idx++;
        }
        return "";
    }
    
    // Get all fields
    std::vector<std::string> GetAllFields(char delimiter = ',') const {
        std::vector<std::string> fields;
        std::stringstream ss(raw_line_);
        std::string field;
        
        while (std::getline(ss, field, delimiter)) {
            fields.push_back(field);
        }
        return fields;
    }
    
private:
    std::string raw_line_;
};

class DataProcessor {
public:
    DataProcessor(const std::string& dataset_path);
    
    // Load entire dataset
    bool LoadDataset();
    
    // Get chunk of data for processing (start_idx to end_idx)
    std::vector<CSVRow> GetChunk(size_t start_idx, size_t count);
    
    // Get total row count
    size_t GetTotalRows() const { return data_.size(); }
    
    // Process a chunk (returns CSV string with header + data)
    std::string ProcessChunk(const std::vector<CSVRow>& chunk, const std::string& filter_column = "", const std::string& filter_value = "");
    
    // Get header
    std::string GetHeader() const { return header_; }
    
private:
    std::string dataset_path_;
    std::string header_;
    std::vector<CSVRow> data_;
};
