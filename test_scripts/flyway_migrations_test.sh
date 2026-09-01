#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NETWORK="owanalytics-flyway-test-$$"
POSTGRES_CONTAINER="owanalytics-postgres-$$"
POSTGRES_IMAGE="${POSTGRES_IMAGE:-postgres:16-alpine}"
FLYWAY_IMAGE="${FLYWAY_IMAGE:-redgate/flyway:13.4.0}"
DB_NAME="${DB_NAME:-owanalytics}"
DB_USER="${DB_USER:-owanalytics}"
DB_PASSWORD="${DB_PASSWORD:-owanalytics}"
FLYWAY_URL="jdbc:postgresql://${POSTGRES_CONTAINER}:5432/${DB_NAME}"
FLYWAY_BASELINE_ON_MIGRATE="${FLYWAY_BASELINE_ON_MIGRATE:-false}"

cleanup() {
    docker rm -f "${POSTGRES_CONTAINER}" >/dev/null 2>&1 || true
    docker network rm "${NETWORK}" >/dev/null 2>&1 || true
    rm -rf "${TMPDIR:-/tmp}/owanalytics-flyway-"*
}
trap cleanup EXIT

run_flyway() {
    docker run --rm \
        --network "${NETWORK}" \
        -v "$1:/flyway/sql:ro" \
        -e "FLYWAY_URL=${FLYWAY_URL}" \
        -e "FLYWAY_USER=${DB_USER}" \
        -e "FLYWAY_PASSWORD=${DB_PASSWORD}" \
        -e "FLYWAY_BASELINE_ON_MIGRATE=${FLYWAY_BASELINE_ON_MIGRATE}" \
        -e "FLYWAY_BASELINE_VERSION=1" \
        -e "FLYWAY_BASELINE_DESCRIPTION=existing-schema" \
        "${FLYWAY_IMAGE}" \
        "${@:2}"
}

psql_exec() {
    docker exec -i \
        -e "PGPASSWORD=${DB_PASSWORD}" \
        "${POSTGRES_CONTAINER}" \
        psql -U "${DB_USER}" -d "${DB_NAME}" -v ON_ERROR_STOP=1 "$@"
}

assert_sql() {
    local sql="$1"
    local expected="$2"
    local actual
    actual="$(psql_exec -Atc "${sql}")"
    if [[ "${actual}" != "${expected}" ]]; then
        echo "Assertion failed: ${sql}" >&2
        echo "Expected: ${expected}" >&2
        echo "Actual: ${actual}" >&2
        exit 1
    fi
}

verify_legacy_schema_for_baseline() {
    psql_exec < "${ROOT_DIR}/scripts/verify_postgresql_v1_schema.sql"
}

docker network create "${NETWORK}" >/dev/null
docker run -d \
    --name "${POSTGRES_CONTAINER}" \
    --network "${NETWORK}" \
    -e "POSTGRES_DB=${DB_NAME}" \
    -e "POSTGRES_USER=${DB_USER}" \
    -e "POSTGRES_PASSWORD=${DB_PASSWORD}" \
    "${POSTGRES_IMAGE}" >/dev/null

until psql_exec -Atc "select 1" >/dev/null 2>&1; do
    sleep 1
done

echo "Fresh database migration"
run_flyway "${ROOT_DIR}/migrations" migrate
run_flyway "${ROOT_DIR}/migrations" validate
run_flyway "${ROOT_DIR}/migrations" info

assert_sql "select count(*) from information_schema.tables where table_schema = current_schema() and table_name in ('boards','timepoints','wificlienthistory','flyway_schema_history');" "4"
assert_sql "select count(*) from information_schema.columns where table_schema = current_schema() and table_name = 'boards' and column_name in ('id','name','description','notes','created','modified','venuelist');" "7"
assert_sql "select count(*) from information_schema.columns where table_schema = current_schema() and table_name = 'timepoints' and column_name in ('id','boardid','timestamp','ap_data','ssid_data','radio_data','device_info','serialnumber');" "8"
assert_sql "select count(*) from information_schema.columns where table_schema = current_schema() and table_name = 'wificlienthistory';" "37"
assert_sql "select count(*) from pg_indexes where schemaname = current_schema() and indexname in ('boards_name_index','timepoint_board_index','timepoint_serial_time_index','stationid_name_index','station_ven_ts_id_name_index');" "5"
assert_sql "select count(*) from flyway_schema_history where success = true;" "1"

echo "Migration re-run"
run_flyway "${ROOT_DIR}/migrations" migrate
assert_sql "select count(*) from flyway_schema_history where success = true;" "1"

echo "Checksum validation"
checksum_dir="$(mktemp -d "${TMPDIR:-/tmp}/owanalytics-flyway-checksum-XXXXXX")"
cp -R "${ROOT_DIR}/migrations/." "${checksum_dir}/"
printf '\n-- checksum test mutation\n' >> "${checksum_dir}/V1__initial_schema.sql"
if run_flyway "${checksum_dir}" validate; then
    echo "Expected checksum validation failure" >&2
    exit 1
fi

echo "Failed migration"
failure_dir="$(mktemp -d "${TMPDIR:-/tmp}/owanalytics-flyway-failure-XXXXXX")"
cp -R "${ROOT_DIR}/migrations/." "${failure_dir}/"
cat > "${failure_dir}/V99__intentional_failure.sql" <<'SQL'
SELECT * FROM table_that_does_not_exist_for_test;
SQL
if run_flyway "${failure_dir}" migrate; then
    echo "Expected migration failure" >&2
    exit 1
fi

echo "Existing database baseline"
docker rm -f "${POSTGRES_CONTAINER}" >/dev/null
docker run -d \
    --name "${POSTGRES_CONTAINER}" \
    --network "${NETWORK}" \
    -e "POSTGRES_DB=${DB_NAME}" \
    -e "POSTGRES_USER=${DB_USER}" \
    -e "POSTGRES_PASSWORD=${DB_PASSWORD}" \
    "${POSTGRES_IMAGE}" >/dev/null
until psql_exec -Atc "select 1" >/dev/null 2>&1; do
    sleep 1
done

legacy_dir="$(mktemp -d "${TMPDIR:-/tmp}/owanalytics-flyway-legacy-XXXXXX")"
cp "${ROOT_DIR}/migrations/V1__initial_schema.sql" "${legacy_dir}/legacy.sql"
psql_exec < "${legacy_dir}/legacy.sql"
psql_exec -c "insert into boards (id, name, description, notes, created, modified, venuelist) values ('board-1', 'Board 1', '', '[]', 1, 1, '[]');"

verify_legacy_schema_for_baseline
FLYWAY_BASELINE_ON_MIGRATE=true run_flyway "${ROOT_DIR}/migrations" migrate
run_flyway "${ROOT_DIR}/migrations" validate
assert_sql "select count(*) from boards where id = 'board-1';" "1"
assert_sql "select count(*) from flyway_schema_history where version = '1' and type = 'BASELINE';" "1"
assert_sql "select count(*) from flyway_schema_history where success = true;" "1"

echo "Invalid existing database baseline"
docker rm -f "${POSTGRES_CONTAINER}" >/dev/null
docker run -d \
    --name "${POSTGRES_CONTAINER}" \
    --network "${NETWORK}" \
    -e "POSTGRES_DB=${DB_NAME}" \
    -e "POSTGRES_USER=${DB_USER}" \
    -e "POSTGRES_PASSWORD=${DB_PASSWORD}" \
    "${POSTGRES_IMAGE}" >/dev/null
until psql_exec -Atc "select 1" >/dev/null 2>&1; do
    sleep 1
done
psql_exec -c "create table unrelated_table (id integer);"
if verify_legacy_schema_for_baseline; then
    echo "Expected legacy schema verification failure" >&2
    exit 1
fi

echo "Invalid same-name schema baseline"
docker rm -f "${POSTGRES_CONTAINER}" >/dev/null
docker run -d \
    --name "${POSTGRES_CONTAINER}" \
    --network "${NETWORK}" \
    -e "POSTGRES_DB=${DB_NAME}" \
    -e "POSTGRES_USER=${DB_USER}" \
    -e "POSTGRES_PASSWORD=${DB_PASSWORD}" \
    "${POSTGRES_IMAGE}" >/dev/null
until psql_exec -Atc "select 1" >/dev/null 2>&1; do
    sleep 1
done
psql_exec < "${legacy_dir}/legacy.sql"
psql_exec -c "alter table boards alter column name type integer using 0;"
if verify_legacy_schema_for_baseline; then
    echo "Expected same-name schema type verification failure" >&2
    exit 1
fi

echo "Invalid same-name index baseline"
docker rm -f "${POSTGRES_CONTAINER}" >/dev/null
docker run -d \
    --name "${POSTGRES_CONTAINER}" \
    --network "${NETWORK}" \
    -e "POSTGRES_DB=${DB_NAME}" \
    -e "POSTGRES_USER=${DB_USER}" \
    -e "POSTGRES_PASSWORD=${DB_PASSWORD}" \
    "${POSTGRES_IMAGE}" >/dev/null
until psql_exec -Atc "select 1" >/dev/null 2>&1; do
    sleep 1
done
psql_exec < "${legacy_dir}/legacy.sql"
psql_exec -c "drop index timepoint_board_index;"
psql_exec -c "create index timepoint_board_index on timepoints (timestamp asc, boardid asc);"
if verify_legacy_schema_for_baseline; then
    echo "Expected same-name schema index verification failure" >&2
    exit 1
fi

echo "Flyway migration tests passed"
