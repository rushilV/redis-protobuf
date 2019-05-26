/**************************************************************************
   Copyright (c) 2019 sewenew

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 *************************************************************************/

#include "field_ref.h"
#include <google/protobuf/util/json_util.h>
#include "redis_protobuf.h"

namespace sw {

namespace redis {

namespace pb {

Path::Path(const StringView &str) {
    const auto *ptr = str.data();
    assert(ptr != nullptr);

    auto len = str.size();

    _type = _parse_type(ptr, len);

    if (_type.size() < len) {
        // Has fields.
        _fields = _parse_fields(ptr + _type.size(), len - _type.size());
    }
}

std::string Path::_parse_type(const char *ptr, std::size_t len) {
    assert(ptr != nullptr);

    if (len == 0) {
        throw Error("empty type");
    }

    std::size_t idx = 0;
    for (; idx != len; ++idx) {
        // e.g. type[field1][field2]
        if (ptr[idx] == '[') {
            break;
        }
    }

    return std::string(ptr, idx);
}

std::vector<std::string> Path::_parse_fields(const char *ptr, std::size_t len) {
    assert(ptr != nullptr && len > 0 && *ptr == '[');

    std::vector<std::string> fields;
    std::size_t start = len;
    std::size_t stop = 0;
    for (std::size_t idx = 0; idx != len; ++idx) {
        if (ptr[idx] == '[') {
            start = idx;
        } else if (ptr[idx] == ']') {
            stop = idx;
            if (stop <= start) {
                throw Error("invalid field: " + std::string(ptr, len));
            }

            if (stop == start + 1) {
                throw Error("empty field: " + std::string(ptr, len));
            }

            fields.emplace_back(ptr + start + 1, stop - start - 1);

            start = len;
        }
    }

    if (stop != len - 1) {
        throw Error("invalid field: " + std::string(ptr, len));
    }

    return fields;
}

FieldRef::FieldRef(gp::Message *parent_msg, const Path &path) {
    _validate_parameters(parent_msg, path);

    msg = parent_msg;

    auto parent_type = ParentType::MSG;

    for (const auto &field : path.fields()) {
        assert(msg != nullptr);
        const auto *reflection = msg->GetReflection();

        switch (parent_type) {
        case ParentType::MSG:
            parent_type = _msg_field(field, reflection);
            break;

        case ParentType::ARR:
            parent_type = _arr_field(field, reflection);
            break;

        case ParentType::MAP: {
            // TODO: support map
            assert(false);
            break;
        }
        case ParentType::SCALAR: {
            // All fields except the last one, must be a message, array or map.
            throw Error("not a message, array or map");
            break;
        }
        default:
            assert(false);
            break;
        }
    }
}

gp::FieldDescriptor::CppType FieldRef::type() const {
    if (field_desc == nullptr) {
        throw Error("no field specified");
    }

    return field_desc->cpp_type();
}

void FieldRef::_validate_parameters(gp::Message *parent_msg, const Path &path) const {
    assert(parent_msg != nullptr);

    if (parent_msg->GetTypeName() != path.type()) {
        throw Error("type missmatch");
    }
}

FieldRef::ParentType FieldRef::_msg_field(const std::string &field,
        const gp::Reflection *reflection) {
    field_desc = msg->GetDescriptor()->FindFieldByName(field);
    if (field_desc == nullptr) {
        throw Error("field not found: " + field);
    }

    if (type() == gp::FieldDescriptor::CPPTYPE_MESSAGE) {
        msg = reflection->MutableMessage(msg, field_desc);
        return ParentType::MSG;
    } else if (field_desc->is_repeated()) {
        return ParentType::ARR;
    } else if (field_desc->is_map()) {
        // TODO: how to do reflection with map?
        assert(false);
        return ParentType::MAP;
    } else {
        return ParentType::SCALAR;
    }
}

FieldRef::ParentType FieldRef::_arr_field(const std::string &field,
        const gp::Reflection *reflection) {
    assert(field_desc != nullptr && msg != nullptr);

    arr_idx = 0;
    try {
        arr_idx = std::stoi(field);
    } catch (const std::exception &e) {
        throw Error("invalid array index: " + field);
    }

    auto size = reflection->FieldSize(*msg, field_desc);
    if (arr_idx >= size) {
        throw Error("array index is out-of-range: " + field);
    }

    switch (type()) {
    case gp::FieldDescriptor::CPPTYPE_INT32:
    case gp::FieldDescriptor::CPPTYPE_INT64:
    case gp::FieldDescriptor::CPPTYPE_UINT32:
    case gp::FieldDescriptor::CPPTYPE_UINT64:
    case gp::FieldDescriptor::CPPTYPE_DOUBLE:
    case gp::FieldDescriptor::CPPTYPE_FLOAT:
    case gp::FieldDescriptor::CPPTYPE_BOOL:
    case gp::FieldDescriptor::CPPTYPE_STRING:
        return ParentType::SCALAR;

    case gp::FieldDescriptor::CPPTYPE_MESSAGE:
        msg = reflection->MutableRepeatedMessage(msg, field_desc, arr_idx);
        return ParentType::MSG;

    case gp::FieldDescriptor::CPPTYPE_ENUM:
        // TODO: support enum
        assert(false);

    // TODO: support map

    default:
        throw Error("invalid cpp type");
    }
}

}

}

}