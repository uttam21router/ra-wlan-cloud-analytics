CREATE TABLE boards (
    id VARCHAR(64) UNIQUE PRIMARY KEY,
    name TEXT,
    description TEXT,
    notes TEXT,
    created BIGINT,
    modified BIGINT,
    venuelist TEXT
);

CREATE INDEX boards_name_index
    ON boards (name ASC);

CREATE TABLE timepoints (
    id VARCHAR(64) UNIQUE PRIMARY KEY,
    boardid TEXT,
    timestamp BIGINT,
    ap_data TEXT,
    ssid_data TEXT,
    radio_data TEXT,
    device_info TEXT,
    serialnumber TEXT
);

CREATE INDEX timepoint_board_index
    ON timepoints (boardid ASC, timestamp ASC);

CREATE INDEX timepoint_serial_time_index
    ON timepoints (serialnumber ASC, timestamp ASC);

CREATE TABLE wificlienthistory (
    timestamp BIGINT,
    station_id TEXT,
    bssid TEXT,
    ssid TEXT,
    rssi BIGINT,
    rx_bitrate BIGINT,
    rx_chwidth BIGINT,
    rx_mcs BIGINT,
    rx_nss BIGINT,
    rx_vht BOOLEAN,
    tx_bitrate BIGINT,
    tx_chwidth BIGINT,
    tx_mcs BIGINT,
    tx_nss BIGINT,
    tx_vht BOOLEAN,
    rx_bytes BIGINT,
    tx_bytes BIGINT,
    rx_duration BIGINT,
    tx_duration BIGINT,
    rx_packets BIGINT,
    tx_packets BIGINT,
    ipv4 TEXT,
    ipv6 TEXT,
    channel_width BIGINT,
    noise BIGINT,
    tx_power BIGINT,
    channel BIGINT,
    active_ms BIGINT,
    busy_ms BIGINT,
    receive_ms BIGINT,
    mode TEXT,
    ack_signal BIGINT,
    ack_signal_avg BIGINT,
    connected BIGINT,
    inactive BIGINT,
    tx_retries BIGINT,
    venue_id TEXT
);

CREATE INDEX stationid_name_index
    ON wificlienthistory (station_id ASC);

CREATE INDEX station_ven_ts_id_name_index
    ON wificlienthistory (venue_id ASC, station_id ASC, timestamp ASC);
