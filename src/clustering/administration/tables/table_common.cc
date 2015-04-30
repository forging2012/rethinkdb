// Copyright 2010-2014 RethinkDB, all rights reserved.
#include "clustering/administration/tables/table_common.hpp"

#include "clustering/administration/datum_adapter.hpp"
#include "clustering/administration/metadata.hpp"
#include "clustering/table_manager/table_meta_client.hpp"
#include "concurrency/cross_thread_signal.hpp"

common_table_artificial_table_backend_t::common_table_artificial_table_backend_t(
        boost::shared_ptr<semilattice_readwrite_view_t<
            cluster_semilattice_metadata_t> > _semilattice_view,
        table_meta_client_t *_table_meta_client,
        admin_identifier_format_t _identifier_format) :
    semilattice_view(_semilattice_view),
    table_meta_client(_table_meta_client),
    identifier_format(_identifier_format),
    subs([this]() { notify_all(); }, semilattice_view)
{
    semilattice_view->assert_thread();
}

std::string common_table_artificial_table_backend_t::get_primary_key_name() {
    return "id";
}

bool common_table_artificial_table_backend_t::read_all_rows_as_vector(
        signal_t *interruptor_on_caller,
        std::vector<ql::datum_t> *rows_out,
        std::string *error_out) {
    cross_thread_signal_t interruptor(interruptor_on_caller, home_thread());
    on_thread_t thread_switcher(home_thread());
    cluster_semilattice_metadata_t metadata = semilattice_view->get();
    std::map<namespace_id_t, table_config_and_shards_t> configs;
    try {
        table_meta_client->list_configs(&interruptor, &configs);
    } catch (const failed_table_op_exc_t &) {
        *error_out = "Failed to retrieve current configurations for one or more tables "
            "because the server(s) hosting the table(s) are all unreachable.";
        return false;
    }
    rows_out->clear();
    for (const auto &pair : configs) {
        ql::datum_t db_name_or_uuid;
        if (!convert_database_id_to_datum(
                pair.second.config.basic.database, identifier_format, metadata,
                &db_name_or_uuid, nullptr)) {
            db_name_or_uuid = ql::datum_t("__deleted_database__");
        }
        ql::datum_t row;
        if (!format_row(pair.first, db_name_or_uuid, pair.second,
                &interruptor, &row, error_out)) {
            return false;
        }
        rows_out->push_back(row);
    }
    return true;
}

bool common_table_artificial_table_backend_t::read_row(
        ql::datum_t primary_key,
        signal_t *interruptor_on_caller,
        ql::datum_t *row_out,
        std::string *error_out) {
    cross_thread_signal_t interruptor(interruptor_on_caller, home_thread());
    on_thread_t thread_switcher(home_thread());
    cluster_semilattice_metadata_t metadata = semilattice_view->get();
    namespace_id_t table_id;
    std::string dummy_error;
    if (!convert_uuid_from_datum(primary_key, &table_id, &dummy_error)) {
        /* If the primary key was not a valid UUID, then it must refer to a nonexistent
        row. */
        *row_out = ql::datum_t();
        return true;
    }
    try {
        database_id_t db_id;
        name_string_t name;
        table_meta_client->get_name(table_id, &db_id, &name);

        ql::datum_t db_name_or_uuid;
        name_string_t db_name;
        if (!convert_database_id_to_datum(db_id, identifier_format,
                metadata, &db_name_or_uuid, &db_name)) {
            db_name_or_uuid = ql::datum_t("__deleted_database__");
            db_name = name_string_t::guarantee_valid("__deleted_database__");
        }

        try {
            table_config_and_shards_t config;
            table_meta_client->get_config(table_id, &interruptor, &config);
            format_row(table_id, db_name_or_uuid, config, &interruptor, row_out);
            return true;
        } catch (const failed_table_op_exc_t &) {
            *error_out = strprintf("The server(s) hosting table `%s.%s` are currently "
                "unreachable.", db_name.c_str(), name.c_str());
            return false;
        } catch (const std::runtime_error &msg) {
            *error_out = msg.what();
            return false;
        }
    } catch (const no_such_table_exc_t &) {
        *row_out = ql::datum_t();
        return true;
    }
}


