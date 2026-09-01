DO $$
DECLARE
    all_table_count integer;
    flyway_history_count integer;
    mismatch_count integer;
BEGIN
    SELECT count(*) INTO flyway_history_count
    FROM information_schema.tables
    WHERE table_schema = current_schema()
      AND table_name = 'flyway_schema_history';

    IF flyway_history_count = 1 THEN
        RAISE NOTICE 'Flyway schema history exists; skipping legacy baseline verification.';
        RETURN;
    END IF;

    SELECT count(*) INTO all_table_count
    FROM information_schema.tables
    WHERE table_schema = current_schema()
      AND table_type = 'BASE TABLE';

    IF all_table_count = 0 THEN
        RAISE NOTICE 'Empty PostgreSQL schema; Flyway will apply V1 normally.';
        RETURN;
    END IF;

    WITH expected(table_name) AS (
        VALUES
            ('boards'),
            ('timepoints'),
            ('wificlienthistory')
    ),
    actual(table_name) AS (
        SELECT table_name
        FROM information_schema.tables
        WHERE table_schema = current_schema()
          AND table_type = 'BASE TABLE'
    )
    SELECT count(*) INTO mismatch_count
    FROM (
        SELECT table_name FROM expected
        EXCEPT
        SELECT table_name FROM actual
        UNION ALL
        SELECT table_name FROM actual
        EXCEPT
        SELECT table_name FROM expected
    ) mismatches;

    IF mismatch_count <> 0 THEN
        RAISE EXCEPTION 'Non-empty PostgreSQL schema is not exactly the OWAnalytics V1 legacy schema: % table mismatch(es).', mismatch_count;
    END IF;

    WITH expected(table_name, column_name, data_type, character_maximum_length, is_nullable) AS (
        VALUES
            ('boards', 'id', 'character varying', 64, 'NO'),
            ('boards', 'name', 'text', NULL::integer, 'YES'),
            ('boards', 'description', 'text', NULL::integer, 'YES'),
            ('boards', 'notes', 'text', NULL::integer, 'YES'),
            ('boards', 'created', 'bigint', NULL::integer, 'YES'),
            ('boards', 'modified', 'bigint', NULL::integer, 'YES'),
            ('boards', 'venuelist', 'text', NULL::integer, 'YES'),
            ('timepoints', 'id', 'character varying', 64, 'NO'),
            ('timepoints', 'boardid', 'text', NULL::integer, 'YES'),
            ('timepoints', 'timestamp', 'bigint', NULL::integer, 'YES'),
            ('timepoints', 'ap_data', 'text', NULL::integer, 'YES'),
            ('timepoints', 'ssid_data', 'text', NULL::integer, 'YES'),
            ('timepoints', 'radio_data', 'text', NULL::integer, 'YES'),
            ('timepoints', 'device_info', 'text', NULL::integer, 'YES'),
            ('timepoints', 'serialnumber', 'text', NULL::integer, 'YES'),
            ('wificlienthistory', 'timestamp', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'station_id', 'text', NULL::integer, 'YES'),
            ('wificlienthistory', 'bssid', 'text', NULL::integer, 'YES'),
            ('wificlienthistory', 'ssid', 'text', NULL::integer, 'YES'),
            ('wificlienthistory', 'rssi', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'rx_bitrate', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'rx_chwidth', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'rx_mcs', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'rx_nss', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'rx_vht', 'boolean', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_bitrate', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_chwidth', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_mcs', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_nss', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_vht', 'boolean', NULL::integer, 'YES'),
            ('wificlienthistory', 'rx_bytes', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_bytes', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'rx_duration', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_duration', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'rx_packets', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_packets', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'ipv4', 'text', NULL::integer, 'YES'),
            ('wificlienthistory', 'ipv6', 'text', NULL::integer, 'YES'),
            ('wificlienthistory', 'channel_width', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'noise', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_power', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'channel', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'active_ms', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'busy_ms', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'receive_ms', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'mode', 'text', NULL::integer, 'YES'),
            ('wificlienthistory', 'ack_signal', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'ack_signal_avg', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'connected', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'inactive', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'tx_retries', 'bigint', NULL::integer, 'YES'),
            ('wificlienthistory', 'venue_id', 'text', NULL::integer, 'YES')
    ),
    actual AS (
        SELECT table_name, column_name, data_type, character_maximum_length, is_nullable
        FROM information_schema.columns
        WHERE table_schema = current_schema()
          AND table_name IN ('boards', 'timepoints', 'wificlienthistory')
    )
    SELECT count(*) INTO mismatch_count
    FROM (
        SELECT table_name, column_name, data_type, character_maximum_length, is_nullable
        FROM expected
        EXCEPT
        SELECT table_name, column_name, data_type, character_maximum_length, is_nullable
        FROM actual
        UNION ALL
        SELECT table_name, column_name, data_type, character_maximum_length, is_nullable
        FROM actual
        EXCEPT
        SELECT table_name, column_name, data_type, character_maximum_length, is_nullable
        FROM expected
    ) mismatches;

    IF mismatch_count <> 0 THEN
        RAISE EXCEPTION 'OWAnalytics V1 schema verification failed: % column definition mismatch(es).', mismatch_count;
    END IF;

    WITH expected(table_name, columns) AS (
        VALUES
            ('boards', 'id'),
            ('timepoints', 'id')
    ),
    actual AS (
        SELECT cls.relname AS table_name,
               string_agg(att.attname, ',' ORDER BY keys.ordinality) AS columns
        FROM pg_constraint con
        JOIN pg_class cls ON cls.oid = con.conrelid
        JOIN pg_namespace ns ON ns.oid = cls.relnamespace
        JOIN LATERAL unnest(con.conkey) WITH ORDINALITY AS keys(attnum, ordinality) ON true
        JOIN pg_attribute att ON att.attrelid = cls.oid AND att.attnum = keys.attnum
        WHERE ns.nspname = current_schema()
          AND con.contype = 'p'
          AND cls.relname IN ('boards', 'timepoints', 'wificlienthistory')
        GROUP BY cls.relname
    )
    SELECT count(*) INTO mismatch_count
    FROM (
        SELECT table_name, columns FROM expected
        EXCEPT
        SELECT table_name, columns FROM actual
        UNION ALL
        SELECT table_name, columns FROM actual
        EXCEPT
        SELECT table_name, columns FROM expected
    ) mismatches;

    IF mismatch_count <> 0 THEN
        RAISE EXCEPTION 'OWAnalytics V1 schema verification failed: % primary-key mismatch(es).', mismatch_count;
    END IF;

    WITH expected(index_name, table_name, is_unique, columns) AS (
        VALUES
            ('boards_name_index', 'boards', false, 'name:ASC'),
            ('timepoint_board_index', 'timepoints', false, 'boardid:ASC,timestamp:ASC'),
            ('timepoint_serial_time_index', 'timepoints', false, 'serialnumber:ASC,timestamp:ASC'),
            ('stationid_name_index', 'wificlienthistory', false, 'station_id:ASC'),
            ('station_ven_ts_id_name_index', 'wificlienthistory', false, 'venue_id:ASC,station_id:ASC,timestamp:ASC')
    ),
    actual AS (
        SELECT idx_cls.relname AS index_name,
               tbl_cls.relname AS table_name,
               idx.indisunique AS is_unique,
               string_agg(
                   att.attname || ':' ||
                   CASE WHEN (COALESCE(opts.option, 0) & 1) = 1 THEN 'DESC' ELSE 'ASC' END,
                   ',' ORDER BY keys.ordinality
               ) AS columns
        FROM pg_index idx
        JOIN pg_class idx_cls ON idx_cls.oid = idx.indexrelid
        JOIN pg_class tbl_cls ON tbl_cls.oid = idx.indrelid
        JOIN pg_namespace ns ON ns.oid = tbl_cls.relnamespace
        JOIN LATERAL unnest(idx.indkey) WITH ORDINALITY AS keys(attnum, ordinality) ON true
        LEFT JOIN LATERAL unnest(idx.indoption) WITH ORDINALITY AS opts(option, ordinality)
            ON opts.ordinality = keys.ordinality
        JOIN pg_attribute att ON att.attrelid = tbl_cls.oid AND att.attnum = keys.attnum
        WHERE ns.nspname = current_schema()
          AND idx_cls.relname IN (
              'boards_name_index',
              'timepoint_board_index',
              'timepoint_serial_time_index',
              'stationid_name_index',
              'station_ven_ts_id_name_index'
          )
        GROUP BY idx_cls.relname, tbl_cls.relname, idx.indisunique
    )
    SELECT count(*) INTO mismatch_count
    FROM (
        SELECT index_name, table_name, is_unique, columns FROM expected
        EXCEPT
        SELECT index_name, table_name, is_unique, columns FROM actual
        UNION ALL
        SELECT index_name, table_name, is_unique, columns FROM actual
        EXCEPT
        SELECT index_name, table_name, is_unique, columns FROM expected
    ) mismatches;

    IF mismatch_count <> 0 THEN
        RAISE EXCEPTION 'OWAnalytics V1 schema verification failed: % index definition mismatch(es).', mismatch_count;
    END IF;

    RAISE NOTICE 'Verified OWAnalytics V1 PostgreSQL schema.';
END $$;
