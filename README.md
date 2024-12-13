# pg_pandas: PostgreSQL + Pandas Extension

`pg_pandas` is an extension for PostgreSQL that integrates the powerful data processing capabilities of Pandas directly within PostgreSQL. It leverages background workers to efficiently execute Pandas operations on SQL query results and return the processed data as PostgreSQL tuples.

---

## Features

- **Pandas Integration**: Apply Pandas operations directly to SQL data.
- **Background Worker**: Utilize persistent Python environments within background workers for efficient processing.
- **Flexible Input**: Supports both subqueries and direct values as input data.
- **JSON Support**: Uses JSON for seamless data serialization between PostgreSQL and Pandas.
- **Parallel Workers**: Supports multiple background workers to handle concurrent Pandas operations.

---

## Installation

### Prerequisites

- **PostgreSQL**: Version 12 or higher.
- **Python**: Version 3.7 or higher.
- **Pandas**: Install via `pip install pandas`.
- **cJSON**: For example, on Debian-based systems, install using `sudo apt-get install libcjson-dev`.

### Steps

1. **Clone Repository**
    ```bash
    git clone https://github.com/yourusername/pg_pandas.git
    cd pg_pandas
    ```

2. **Run Configuration Script**
    ```bash
    ./configure
    ```
    - This script checks for Python3, Pandas, and PostgreSQL development files, then generates the Makefile.
    - By default, it uses the `$PGDATA/postgresql.conf` path, but you can specify a custom path as an argument:
    ```bash
    ./configure /path/to/your/postgresql/data
    ```

3. **Compile and Install**
    ```bash
    make
    sudo make install
    ```

4. **Configure PostgreSQL**
    - **Load Extension**
        Add the extension to PostgreSQL's `shared_preload_libraries` in `postgresql.conf`:
        ```conf
        shared_preload_libraries = 'pg_pandas'
        ```
    - **Restart PostgreSQL**
        ```bash
        sudo service postgresql restart
        ```
    - **Create Extension**
        ```sql
        CREATE EXTENSION pg_pandas;
        ```

5. **Allocate Shared Memory**
    Ensure that PostgreSQL has sufficient shared memory allocated. Modify `postgresql.conf` if necessary:
    ```conf
    shared_buffers = 256MB
    ```

6. **Set Parallel Workers**
    Configure the number of parallel workers by setting `pg_pandas.parallel` in `postgresql.conf`:
    ```conf
    pg_pandas.parallel = 4  # Example: 4 parallel workers
    ```
    > **Note:** Adjust the number based on your system's CPU and memory resources.

---

## Usage

### Applying Pandas Operations

You can use the `pandas` function to apply Pandas operations on SQL query results. Here's how you can use it:

1. **Basic Aggregation**
    ```sql
    SELECT * FROM pandas(
      (SELECT * FROM sales_data),
      'lambda df: df.groupby("region").sum().reset_index()'
    ) AS t(region text, total_sales float8);
    ```

2. **Dataframe Merging**
    ```sql
    SELECT * FROM pandas(
      (SELECT * FROM table1),
      'lambda df1: df1.merge(pd.read_json(\'[{"id": 1, "extra": "A"}, {"id": 2, "extra": "B"}]\'), on="id")'
    ) AS t(id int, column1 text, extra text);
    ```

3. **Time Series Resampling**
    ```sql
    SELECT * FROM pandas(
      (SELECT timestamp, value FROM timeseries_data),
      'lambda df: df.set_index("timestamp").resample("1D").sum().reset_index()'
    ) AS t(timestamp timestamp, total_value float8);
    ```

---

## Configuration

### pg_pandas.parallel

The `pg_pandas.parallel` parameter controls the number of parallel background workers that `pg_pandas` uses to handle concurrent Pandas operations. Adjusting this parameter allows you to optimize performance based on your system's capabilities.

- **Type:** `integer`
- **Range:** `1` to `1024`
- **Default:** `1`

**Example Setting:**

```conf
# postgresql.conf

# pg_pandas settings
pg_pandas.parallel = 4  # Activate 4 parallel workers
```


> **Caution:** Increasing the number of parallel workers will consume more system resources. Ensure that your system has sufficient CPU and memory to handle the specified number of workers.

---

## Internal Workings

1. **Background Worker Initialization:**
   - When PostgreSQL starts, `pg_pandas` initializes multiple background workers based on the `pg_pandas.parallel` setting.
   - Each worker connects to a shared memory segment to listen for incoming Pandas operation tasks.

2. **Data Processing Flow:**
   - When a user invokes the `pandas` function, the input data is serialized to JSON and stored in shared memory along with the specified Pandas operation.
   - An available background worker picks up the task, executes the Pandas operation within a restricted Python environment, and serializes the result back to JSON.
   - The processed data is then returned to PostgreSQL as a set of tuples.

3. **Memory Management:**
   - Utilizes PostgreSQL's shared memory (`ShmemInitStruct`) and lightweight locks (`LWLock`) to manage synchronization between the main backend process and background workers.
   - Employs memory contexts (`MemoryContext`) to efficiently handle memory allocation and cleanup.
   - Python environments within workers are persistent to minimize initialization overhead.

---

## Memory Management

- **Shared Memory:** Uses PostgreSQL's shared memory to facilitate communication between backend processes and background workers.
- **Locks:** Employs `LWLock` to synchronize access to shared memory structures, preventing race conditions.
- **Memory Contexts:** Utilizes PostgreSQL's memory contexts (`palloc`) for efficient memory allocation and management.
- **Python Environment:** Maintains a persistent Python environment within each background worker to minimize initialization overhead.
- **Data Handling:** Uses JSON for efficient serialization and deserialization between PostgreSQL and Python's Pandas.

---

## Limitations and Considerations

1. **Security:**
   - The `operation` parameter allows arbitrary Python code execution, posing potential security risks.
   - **Mitigation:**
     - The `initialize_secure_python` function restricts the execution environment to specific Python modules (`pandas`, `numpy`, `json`) and limits built-in functions (`print`, `len`, `range`).
     - **Recommendation:** Ensure that only trusted users have the necessary permissions to execute the `pandas` function.

2. **Performance:**
   - Large datasets can increase memory usage and processing time.
   - Optimize data sizes and Pandas operations to maintain performance.
   - Adjust the `pg_pandas.parallel` parameter to balance concurrency and resource utilization based on your system's capabilities.

3. **Error Handling:**
   - Errors during Python code execution are logged in PostgreSQL's error logs.
   - Implement comprehensive logging and monitoring to effectively track and resolve issues.

4. **Concurrency:**
   - Supports multiple background workers to handle concurrent Pandas operations.
   - Adjust the `pg_pandas.parallel` parameter to balance between performance gains and resource utilization.
   - Single-worker configurations may lead to bottlenecks under high concurrency; utilizing multiple workers can alleviate this.

---

## Contributing

Contributions are welcome! Please open issues or submit pull requests for enhancements, bug fixes, or new features.

---

## License

This project is licensed under the BSD3 License. See the [LICENSE file](./LICENSE) for details.