#include "DataProcessor.h"
#include <iostream>
#include <algorithm>

DataProcessor::DataProcessor(const std::string& dataset_path) 
    : dataset_path_(dataset_path), header_("") {
}

bool DataProcessor::LoadDataset() {
    std::ifstream file(dataset_path_);
    if (!file.is_open()) {
        std::cerr << "[DataProcessor] can't open dataset: " << dataset_path_ << std::endl;
        return false;
    }
    
    std::cout << "[DataProcessor] loading " << dataset_path_ << std::endl;
    
    // Read and store header
    std::getline(file, header_);
    
    int row_count = 0;
    int empty_lines = 0;
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty()) {
            empty_lines++;
            continue;
        }
        
        // Store raw line as CSVRow
        data_.emplace_back(line);
        row_count++;
        
        // Progress indicator for large files
        if (row_count % 100000 == 0) {
            std::cout << "[DataProcessor] Progress: " << row_count << " rows loaded" << std::endl;
        }
    }
    
    file.close();
    std::cout << "[DataProcessor] loaded " << row_count << " row(s)" << std::endl;
    
    return row_count > 0;
}

std::vector<CSVRow> DataProcessor::GetChunk(size_t start_idx, size_t count) {
    std::vector<CSVRow> chunk;
    
    if (start_idx >= data_.size()) {
        std::cerr << "[DataProcessor] bad start_idx " << start_idx 
              << " (size=" << data_.size() << ")" << std::endl;
        return chunk;
    }
    
    size_t end_idx = std::min(start_idx + count, data_.size());
    for (size_t i = start_idx; i < end_idx; i++) {
        chunk.push_back(data_[i]);
    }
    
    std::cout << "[DataProcessor] chunk start=" << start_idx 
              << " requested=" << count << " actual=" << chunk.size() << std::endl;
    return chunk;
}

std::string DataProcessor::ProcessChunk(const std::vector<CSVRow>& chunk, const std::string& filter_column, const std::string& filter_value) {
    std::stringstream ss;
    
    // Add header
    ss << header_ << "\n";
    
    int processed = 0; // Count of processed rows in terms of filtering
    for (const auto& row : chunk) {
        // Apply filter if specified
        if (!filter_column.empty() && !filter_value.empty()) {
            // Parse header to find column index
            std::stringstream header_ss(header_);
            std::string col_name;
            int col_index = -1;
            int current_col = 0;
            
            while (std::getline(header_ss, col_name, ',')) {
                if (col_name == filter_column) {
                    col_index = current_col;
                    break;
                }
                current_col++;
            }
            
            // Check if row matches filter
            if (col_index >= 0) {
                std::string field_value = row.GetField(col_index);
                if (field_value != filter_value) {
                    continue;  // Skip this row
                }
            }
        }
        
        // Write raw row
        ss << row.GetRaw() << "\n";
        processed++;
    }
    
    std::cout << "[DataProcessor] processed=" << processed;
    if (!filter_column.empty()) {
        std::cout << " filter=" << filter_column << "=" << filter_value;
    }
    std::cout << std::endl;
    
    return ss.str();
}
