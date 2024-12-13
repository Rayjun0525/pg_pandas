# pg_pandas: PostgreSQL + Pandas Extension

`pg_pandas` is a PostgreSQL extension that integrates the powerful data processing capabilities of Pandas directly into PostgreSQL. Leveraging a background worker, this extension allows you to execute Pandas operations on SQL query results efficiently and return the processed data as PostgreSQL tuples.

---

## Features

- **Pandas Integration**: Apply any Pandas operation on SQL data directly within PostgreSQL.
- **Background Worker**: Maintains a persistent Python environment for efficient processing.
- **Flexible Input**: Accepts subqueries or direct values as input data.
- **JSON Support**: Handles JSON data for seamless data exchange between PostgreSQL and Pandas.

---

## Installation

### Prerequisites

- **PostgreSQL**: Version 12 or higher.
- **Python**: Version 3.7 or higher.
- **Pandas**: Install via `pip install pandas`.
- **cJSON**: Install the cJSON library (e.g., `sudo apt-get install libcjson-dev` on Debian-based systems).

### Steps

1. **Clone the Repository**

   ```bash
   git clone https://github.com/yourusername/pg_pandas.git
   cd pg_pandas
   ```

2. Run the Configuration Script

  ```bash
  ./configure
  ```
  
  This script checks for Python3, Pandas, and PostgreSQL development files, then generates the Makefile.

3. Compile and Install

  ```bash
  make
  sudo make install
  ```
4. Configure PostgreSQL

  Ensure that the shared library path is included in PostgreSQL's `shared_preload_libraries`. Add the following line to your `postgresql.conf`:

  ```conf
  shared_preload_libraries = 'pg_pandas'
  ```
  Note: This requires a PostgreSQL server restart.

5. Create the Extension in PostgreSQL

  ```sql
  CREATE EXTENSION pg_pandas;
  ```

---

## Usage

### Basic Usage

1. Applying a Pandas Operation on a Subquery

  ```sql
  SELECT * FROM pandas(
    (SELECT id, value FROM your_table WHERE status = 'active'),
    'lambda df: df[df["value"] > 10]'
  ) AS t(id int, value float8);
  ```

2. Applying a Pandas Operation on Direct Values

  ```sql
  -- Single Value
  SELECT * FROM pandas(
    42,
    'lambda df: pd.DataFrame({"result": [df + 10]})'
  ) AS t(result int);

  -- JSON Array
  SELECT * FROM pandas(
    '[{"id": 1, "value": 10}, {"id": 2, "value": 20}]',
    'lambda df: df[df["value"] > 15]'
  ) AS t(id int, value float8);
  ```

### Advanced Usage

1. GroupBy and Aggregation

  ```sql
  SELECT * FROM pandas(
    (SELECT category, amount FROM sales),
    'lambda df: df.groupby("category").sum().reset_index()'
  ) AS t(category text, total_amount float8);
  ```
  
2. Adding a New Column

  ```sql
  SELECT * FROM pandas(
    (SELECT id, value FROM your_table),
    'lambda df: df.assign(new_col=df["value"] * 2)'
  ) AS t(id int, value float8, new_col float8);
  ```

3. Merging DataFrames

  ```sql
  SELECT * FROM pandas(
    (SELECT * FROM table1),
    'lambda df1: df1.merge(pd.read_json(\'[{"id": 1, "extra": "A"}, {"id": 2, "extra": "B"}]\'), on="id")'
  ) AS t(id int, column1 text, extra text);
  ```

4. Time Series Resampling

  ```sql
  SELECT * FROM pandas(
    (SELECT timestamp, value FROM timeseries_data),
    'lambda df: df.set_index("timestamp").resample("1D").sum().reset_index()'
  ) AS t(timestamp timestamp, total_value float8);
  ```

---

## Internal Workings
1. Background Worker:

* Upon PostgreSQL server start, the background worker initializes a persistent Python environment with Pandas loaded.
* It listens for data and operations sent from the main PostgreSQL backend.

2. SQL Function `pandas(data, operation)`:

* `data`: Can be a subquery result or direct values, automatically converted to JSON.
* `operation`: A Python lambda function string that defines the Pandas operation to perform on the data.

3. Data Flow:

* The `pandas` function sends the JSON-serialized data and operation to the background worker via shared memory.
* The background worker processes the data using Pandas and returns the result as a JSON string.
* The main PostgreSQL backend parses the JSON result and returns it as a set of records.

---

## Memory Management

* **Shared Memory**: Utilizes PostgreSQL's shared memory (`ShmemInitStruct`) to facilitate communication between the backend and the background worker.

* **Locks**: Employs `LWLock` to ensure synchronized access to shared memory structures, preventing race conditions.

* **Memory Contexts**: Allocates memory within PostgreSQL's memory contexts (`palloc`) to ensure proper memory management and cleanup.

* **Python Environment**: The background worker maintains a persistent Python environment, reducing the overhead of initializing Python for each operation.
* **Data Handling**: Ensures that all data passed between PostgreSQL and Python is serialized/deserialized appropriately to prevent memory leaks and ensure data integrity.

---

## Limitations and Considerations
1. Security:

* The `operation` parameter executes arbitrary Python code. Ensure that only trusted users can execute this function to prevent security risks.
* Consider implementing a whitelist of allowed operations or sandboxing the Python execution environment.

2. Performance:

* Large datasets may lead to high memory usage. Monitor and optimize the size of data being processed.
* Network latency is minimal as the processing occurs within the PostgreSQL server, but Python execution speed should be considered.

3. Error Handling:

* Errors in Python code will propagate back to PostgreSQL. Ensure proper error handling and logging mechanisms are in place.

4. Concurrency:

* The current implementation assumes a single background worker. For high concurrency, consider extending the implementation to support multiple workers.

---

## Contributing

Contributions are welcome! Please open issues or submit pull requests for enhancements, bug fixes, or new features.

---

## License

This project is licensed under the BSD3 License. See the [LICENSE file](./LICENSE) for details.