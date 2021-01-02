// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#pragma once

#include <stdint.h>

#include <algorithm>
#include <cassert>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <regex>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>

#include "open3d/core/Dtype.h"
#include "open3d/core/SizeVector.h"

namespace open3d {
namespace core {

inline char BigEndianChar() {
    int x = 1;
    return (((char*)&x)[0]) ? '<' : '>';
}

inline char DtypeToChar(const Dtype& dtype) {
    // Not all dtypes are supported.
    // 'f': float, double, long double
    // 'i': int, char, short, long, long long
    // 'u': unsigned char, unsigned short, unsigned long, unsigned long long,
    //      unsigned int
    // 'b': bool
    // 'c': std::complex<float>, std::complex<double>),
    //      std::complex<long double>)
    // '?': object
    if (dtype == Dtype::Float32) return 'f';
    if (dtype == Dtype::Float64) return 'f';
    if (dtype == Dtype::Int32) return 'i';
    if (dtype == Dtype::Int64) return 'i';
    if (dtype == Dtype::UInt8) return 'u';
    if (dtype == Dtype::UInt16) return 'u';
    if (dtype == Dtype::Bool) return 'b';
    utility::LogError("Unsupported dtype: {}", dtype.ToString());
}

template <typename T>
inline std::string ToByteString(const T& rhs) {
    std::stringstream ss;
    for (size_t i = 0; i < sizeof(T); i++) {
        char val = *((char*)&rhs + i);
        ss << val;
    }
    return ss.str();
}

inline std::vector<char> CreateNumpyHeader(const std::vector<size_t>& shape,
                                           const Dtype& dtype) {
    // {}     -> "()"
    // {1}    -> "(1,)"
    // {1, 2} -> "(1, 2)"
    std::stringstream shape_ss;
    if (shape.size() == 0) {
        shape_ss << "()";
    } else if (shape.size() == 1) {
        shape_ss << fmt::format("({},)", shape[0]);
    } else {
        shape_ss << "(";
        shape_ss << shape[0];
        for (size_t i = 1; i < shape.size(); i++) {
            shape_ss << ", ";
            shape_ss << shape[i];
        }
        if (shape.size() == 1) {
            shape_ss << ",";
        }
        shape_ss << ")";
    }

    // Pad with spaces so that preamble+dict is modulo 16 bytes.
    // - Preamble is 10 bytes.
    // - Dict needs to end with '\n'.
    // - Header dict size includes the padding size and '\n'.
    std::string dict = fmt::format(
            "{{'descr': '{}{}{}', 'fortran_order': False, 'shape': {}, }}",
            BigEndianChar(), DtypeToChar(dtype), dtype.ByteSize(),
            shape_ss.str());
    size_t space_padding = 16 - (10 + dict.size()) % 16 - 1;  // {0, 1, ..., 15}
    dict.insert(dict.end(), space_padding, ' ');
    dict += '\n';

    std::stringstream ss;
    // "Magic" values.
    ss << (char)0x93;
    ss << "NUMPY";
    // Major version of numpy format.
    ss << (char)0x01;
    // Minor version of numpy format.
    ss << (char)0x00;
    // Header dict size (full header size - 10).
    ss << ToByteString((uint16_t)dict.size());
    // Header dict.
    ss << dict;

    std::string s = ss.str();
    return std::vector<char>(s.begin(), s.end());
}

class NumpyArray {
public:
    NumpyArray(const std::vector<size_t>& shape,
               char type,
               size_t word_size,
               bool fortran_order)
        : shape_(shape),
          type_(type),
          word_size_(word_size),
          fortran_order_(fortran_order) {
        num_elements_ = 1;
        for (size_t i = 0; i < shape_.size(); i++) {
            num_elements_ *= shape_[i];
        }
        data_holder =
                std::make_shared<std::vector<char>>(num_elements_ * word_size_);
    }

    NumpyArray()
        : shape_(0), word_size_(0), fortran_order_(0), num_elements_(0) {}

    template <typename T>
    T* GetDataPtr() {
        return reinterpret_cast<T*>(&(*data_holder)[0]);
    }

    template <typename T>
    const T* GetDataPtr() const {
        return reinterpret_cast<T*>(&(*data_holder)[0]);
    }

    Dtype GetDtype() const {
        Dtype dtype(Dtype::DtypeCode::Undefined, 1, "undefined");
        if (type_ == 'f' && word_size_ == 4) {
            dtype = Dtype::Float32;
        } else if (type_ == 'f' && word_size_ == 8) {
            dtype = Dtype::Float64;
        } else if (type_ == 'i' && word_size_ == 4) {
            dtype = Dtype::Int32;
        } else if (type_ == 'i' && word_size_ == 8) {
            dtype = Dtype::Int64;
        } else if (type_ == 'u' && word_size_ == 1) {
            dtype = Dtype::UInt8;
        } else if (type_ == 'u' && word_size_ == 2) {
            dtype = Dtype::UInt16;
        } else if (type_ == 'b') {
            dtype = Dtype::Bool;
        }
        if (dtype.GetDtypeCode() == Dtype::DtypeCode::Undefined) {
            utility::LogError("Unsupported Numpy type {} word_size_ {}.", type_,
                              word_size_);
        }
        return dtype;
    }

    SizeVector GetShape() const {
        return SizeVector(shape_.begin(), shape_.end());
    }

    bool GetFortranOrder() const { return fortran_order_; }

    size_t NumBytes() const { return data_holder->size(); }

    static NumpyArray Load(const std::string& file_name) {
        FILE* fp = fopen(file_name.c_str(), "rb");
        if (!fp) {
            utility::LogError("NumpyLoad: Unable to open file {}.", file_name);
        }
        std::vector<size_t> shape;
        size_t word_size;
        bool fortran_order;
        char type;
        ParseNumpyHeader(fp, type, word_size, shape, fortran_order);
        NumpyArray arr(shape, type, word_size, fortran_order);
        size_t nread = fread(arr.GetDataPtr<char>(), 1, arr.NumBytes(), fp);
        if (nread != arr.NumBytes()) {
            utility::LogError("LoadTheNumpyFile: failed fread");
        }
        fclose(fp);
        return arr;
    }

private:
    static void ParseNumpyHeader(FILE* fp,
                                 char& type,
                                 size_t& word_size,
                                 std::vector<size_t>& shape,
                                 bool& fortran_order) {
        char buffer[256];
        size_t res = fread(buffer, sizeof(char), 11, fp);
        if (res != 11) {
            utility::LogError("ParseNumpyHeader: failed fread");
        }
        std::string header = fgets(buffer, 256, fp);
        assert(header[header.size() - 1] == '\n');

        size_t loc1, loc2;

        // Fortran order
        loc1 = header.find("fortran_order");
        if (loc1 == std::string::npos) {
            utility::LogError(
                    "ParseNumpyHeader: failed to find header keyword: "
                    "'fortran_order'");
        }

        loc1 += 16;
        fortran_order = (header.substr(loc1, 4) == "True" ? true : false);

        // shape
        loc1 = header.find("(");
        loc2 = header.find(")");
        if (loc1 == std::string::npos || loc2 == std::string::npos) {
            utility::LogError(
                    "ParseNumpyHeader: failed to find header keyword: '(' or "
                    "')'");
        }

        std::regex num_regex("[0-9][0-9]*");
        std::smatch sm;
        shape.clear();

        std::string str_shape = header.substr(loc1 + 1, loc2 - loc1 - 1);
        while (std::regex_search(str_shape, sm, num_regex)) {
            shape.push_back(std::stoi(sm[0].str()));
            str_shape = sm.suffix().str();
        }

        // Endian, word size, data type
        // byte order code | stands for not applicable.
        // not sure when this applies except for byte array
        loc1 = header.find("descr");
        if (loc1 == std::string::npos) {
            utility::LogError(
                    "ParseNumpyHeader: failed to find header keyword: 'descr'");
        }

        loc1 += 9;
        bool littleEndian =
                (header[loc1] == '<' || header[loc1] == '|' ? true : false);
        assert(littleEndian);
        (void)littleEndian;

        type = header[loc1 + 1];

        std::string str_ws = header.substr(loc1 + 2);
        loc2 = str_ws.find("'");
        word_size = atoi(str_ws.substr(0, loc2).c_str());
    }

    std::shared_ptr<std::vector<char>> data_holder;
    std::vector<size_t> shape_;
    char type_;
    size_t word_size_;
    bool fortran_order_;
    size_t num_elements_;
};

inline void NumpySave(std::string fname,
                      const void* data,
                      const std::vector<size_t>& shape,
                      const Dtype& dtype) {
    FILE* fp = fopen(fname.c_str(), "wb");
    std::vector<char> header = CreateNumpyHeader(shape, dtype);
    size_t num_elements = std::accumulate(shape.begin(), shape.end(), 1,
                                          std::multiplies<size_t>());

    fseek(fp, 0, SEEK_SET);
    fwrite(&header[0], sizeof(char), header.size(), fp);
    fseek(fp, 0, SEEK_END);
    fwrite(data, dtype.ByteSize(), num_elements, fp);
    fclose(fp);
}

}  // namespace core
}  // namespace open3d
