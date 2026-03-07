#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "../src/sqlite3_enhance.h"

/* sagaflow工作流状态 */
typedef enum {
    STATE_PENDING,
    STATE_RUNNING,
    STATE_COMPLETED,
    STATE_FAILED
} WorkflowState;

/* 工作流结构 */
typedef struct {
    int id;
    WorkflowState state;
    char data[1024];
    long updated_at;
} Workflow;

/* 数据库连接池 */
typedef struct {
    sqlite3 *db;
    pthread_mutex_t lock;
} DBConnection;

/* 初始化数据库 */
int init_sagaflow_db(DBConnection *conn, const char *path) {
    sqlite3_enhance_init();

    int rc = sqlite3_open(path, &conn->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开数据库: %s\n", sqlite3_errmsg(conn->db));
        return -1;
    }

    // 启用SQLite-Enhance优化
    sqlite3_exec(conn->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(conn->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    sqlite3_enable_lockfree_writer(conn->db, 1, 8192);
    sqlite3_enable_smart_cache(conn->db, 1, 2000);
    sqlite3_enable_async_io(conn->db, 1, 100);

    sqlite3_exec(conn->db, "PRAGMA cache_size=-8000;", NULL, NULL, NULL);

    // 创建表
    const char *sql =
        "CREATE TABLE IF NOT EXISTS workflows ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  state INTEGER NOT NULL,"
        "  data TEXT,"
        "  updated_at INTEGER NOT NULL"
        ");";

    rc = sqlite3_exec(conn->db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "创建表失败: %s\n", sqlite3_errmsg(conn->db));
        return -1;
    }

    // 创建索引
    sqlite3_exec(conn->db,
        "CREATE INDEX IF NOT EXISTS idx_state ON workflows(state);",
        NULL, NULL, NULL);

    pthread_mutex_init(&conn->lock, NULL);

    printf("✓ 数据库初始化完成\n");
    return 0;
}

/* 创建工作流 */
int create_workflow(DBConnection *conn, Workflow *wf) {
    const char *sql =
        "INSERT INTO workflows (state, data, updated_at) VALUES (?, ?, ?);";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(conn->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, wf->state);
    sqlite3_bind_text(stmt, 2, wf->data, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, wf->updated_at);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        wf->id = sqlite3_last_insert_rowid(conn->db);
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* 更新工作流状态 */
int update_workflow_state(DBConnection *conn, int id, WorkflowState new_state) {
    const char *sql =
        "UPDATE workflows SET state = ?, updated_at = ? WHERE id = ?;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(conn->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, new_state);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    sqlite3_bind_int(stmt, 3, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* 查询工作流 */
int get_workflow(DBConnection *conn, int id, Workflow *wf) {
    const char *sql =
        "SELECT id, state, data, updated_at FROM workflows WHERE id = ?;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(conn->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        wf->id = sqlite3_column_int(stmt, 0);
        wf->state = sqlite3_column_int(stmt, 1);
        strncpy(wf->data, (const char*)sqlite3_column_text(stmt, 2), 1023);
        wf->updated_at = sqlite3_column_int64(stmt, 3);
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_ROW) ? 0 : -1;
}

/* 批量更新工作流（高性能模式） */
int batch_update_workflows(DBConnection *conn, int *ids, WorkflowState *states, int count) {
    const char *sql =
        "UPDATE workflows SET state = ?, updated_at = ? WHERE id = ?;";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(conn->db, sql, -1, &stmt, NULL);

    // 开启事务
    sqlite3_exec(conn->db, "BEGIN;", NULL, NULL, NULL);

    long now = time(NULL);
    for (int i = 0; i < count; i++) {
        sqlite3_bind_int(stmt, 1, states[i]);
        sqlite3_bind_int64(stmt, 2, now);
        sqlite3_bind_int(stmt, 3, ids[i]);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    // 提交事务
    sqlite3_exec(conn->db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_finalize(stmt);

    return 0;
}

/* 性能测试 */
void benchmark_sagaflow(DBConnection *conn) {
    printf("\n=== Sagaflow性能测试 ===\n");

    // 测试1：单条插入
    clock_t start = clock();
    for (int i = 0; i < 100; i++) {
        Workflow wf = {
            .state = STATE_PENDING,
            .data = "test workflow data",
            .updated_at = time(NULL)
        };
        create_workflow(conn, &wf);
    }
    double time_single = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (time_single == 0) time_single = 0.001;
    printf("单条插入 100次: %.2fs (%.2f TPS)\n", time_single, 100/time_single);

    // 测试2：批量更新
    int ids[100];
    WorkflowState states[100];
    for (int i = 0; i < 100; i++) {
        ids[i] = i + 1;
        states[i] = STATE_RUNNING;
    }

    start = clock();
    batch_update_workflows(conn, ids, states, 100);
    double time_batch = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (time_batch == 0) time_batch = 0.001;
    printf("批量更新 100次: %.2fs (%.2f TPS)\n", time_batch, 100/time_batch);

    // 测试3：随机查询
    start = clock();
    for (int i = 0; i < 100; i++) {
        Workflow wf;
        get_workflow(conn, rand() % 100 + 1, &wf);
    }
    double time_query = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (time_query == 0) time_query = 0.001;
    printf("随机查询 100次: %.2fs (%.2f QPS)\n", time_query, 100/time_query);
}

int main() {
    DBConnection conn;

    // 初始化数据库
    if (init_sagaflow_db(&conn, "sagaflow.db") != 0) {
        return 1;
    }

    // 运行性能测试
    benchmark_sagaflow(&conn);

    // 清理
    sqlite3_close(conn.db);
    pthread_mutex_destroy(&conn.lock);
    sqlite3_enhance_cleanup();

    return 0;
}
