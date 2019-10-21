/*
 * Copyright (C) 2014 ScyllaDB
 */

/*
 * This file is part of Scylla.
 *
 * Scylla is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scylla is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sstring.hh>
#include <vector>

#include "types.hh"
#include "collection_mutation.hh"
#include "utils/chunked_vector.hh"
#include "schema_fwd.hh"

class collection_type_impl : public abstract_type {
    static logging::logger _logger;
public:
    static constexpr size_t max_elements = 65535;

protected:
    bool _is_multi_cell;
    explicit collection_type_impl(kind k, sstring name, bool is_multi_cell)
            : abstract_type(k, std::move(name), {}, data::type_info::make_collection()), _is_multi_cell(is_multi_cell) {}
public:
    bool is_multi_cell() const { return _is_multi_cell; }
    virtual data_type name_comparator() const = 0;
    virtual data_type value_comparator() const = 0;
    shared_ptr<cql3::column_specification> make_collection_receiver(shared_ptr<cql3::column_specification> collection, bool is_key) const;
    std::vector<atomic_cell> enforce_limit(std::vector<atomic_cell>, int version) const;
    virtual std::vector<bytes> serialized_values(std::vector<atomic_cell> cells) const = 0;
    bytes serialize_for_native_protocol(std::vector<atomic_cell> cells, int version) const;
    virtual bool is_compatible_with_frozen(const collection_type_impl& previous) const = 0;
    virtual bool is_value_compatible_with_frozen(const collection_type_impl& previous) const = 0;
    template <typename BytesViewIterator>
    static bytes pack(BytesViewIterator start, BytesViewIterator finish, int elements, cql_serialization_format sf);
    bool is_empty(collection_mutation_view in) const;
    bool is_any_live(collection_mutation_view in, tombstone tomb = tombstone(), gc_clock::time_point now = gc_clock::time_point::min()) const;
    api::timestamp_type last_update(collection_mutation_view in) const;
    virtual bytes to_value(collection_mutation_view_description mut, cql_serialization_format sf) const = 0;
    bytes to_value(collection_mutation_view mut, cql_serialization_format sf) const;
    collection_mutation merge(collection_mutation_view a, collection_mutation_view b) const;
    collection_mutation difference(collection_mutation_view a, collection_mutation_view b) const;
    virtual void serialize(const void* value, bytes::iterator& out, cql_serialization_format sf) const = 0;
    virtual data_value deserialize(bytes_view v, cql_serialization_format sf) const = 0;
    data_value deserialize_value(bytes_view v, cql_serialization_format sf) const {
        return deserialize(v, sf);
    }
    bytes_opt reserialize(cql_serialization_format from, cql_serialization_format to, bytes_view_opt v) const;
};

// a list or a set
class listlike_collection_type_impl : public collection_type_impl {
protected:
    data_type _elements;
    explicit listlike_collection_type_impl(kind k, sstring name, data_type elements,bool is_multi_cell);
public:
    const data_type& get_elements_type() const { return _elements; }
    // A list or set value can be serialized as a vector<pair<timeuuid, data_value>> or
    // vector<pair<data_value, empty>> respectively. Compare this representation with
    // vector<data_value> without transforming either of the arguments. Since Cassandra doesn't
    // allow nested multi-cell collections this representation does not transcend to values, and we
    // don't need to worry about recursing.
    // @param this          type of the listlike value represented as vector<data_value>
    // @param map_type      type of the listlike value represented as vector<pair<data_value, data_value>>
    // @param list          listlike value, represented as vector<data_value>
    // @param map           listlike value represented as vector<pair<data_value, data_value>>
    //
    // This function is used to compare receiver with a literal or parameter marker during condition
    // evaluation.
    int32_t compare_with_map(const map_type_impl& map_type, bytes_view list, bytes_view map) const;
    // A list or set value can be represented as a vector<pair<timeuuid, data_value>> or
    // vector<pair<data_value, empty>> respectively. Serialize this representation
    // as a vector of values, not as a vector of pairs.
    bytes serialize_map(const map_type_impl& map_type, const data_value& value) const;
};

template <typename BytesViewIterator>
bytes
collection_type_impl::pack(BytesViewIterator start, BytesViewIterator finish, int elements, cql_serialization_format sf) {
    size_t len = collection_size_len(sf);
    size_t psz = collection_value_len(sf);
    for (auto j = start; j != finish; j++) {
        len += j->size() + psz;
    }
    bytes out(bytes::initialized_later(), len);
    bytes::iterator i = out.begin();
    write_collection_size(i, elements, sf);
    while (start != finish) {
        write_collection_value(i, sf, *start++);
    }
    return out;
}
