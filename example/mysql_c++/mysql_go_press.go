// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

package main

import (
	"database/sql"
	"flag"
	"fmt"
	_ "github.com/go-sql-driver/mysql"
	"log"
	"sync/atomic"
	"time"
)

var thread_num int

func init() {
	flag.IntVar(&thread_num, "thread_num", 1, "thread number")
}

var cost int64
var qps int64 = 1

func main() {
	flag.Parse()

	db, err := sql.Open("mysql", "brpcuser:12345678@tcp(127.0.0.1:3306)/brpc_test?charset=utf8")
	if err != nil {
		log.Fatal(err)
	}

	for i := 0; i < thread_num; i++ {
		go func() {
			for {
				var (
					id   int
					col1 string
					col2 string
					col3 string
					col4 string
				)
				start := time.Now()
				rows, err := db.Query("select * from brpc_press where id = 1")
				if err != nil {
					log.Fatal(err)
				}
				for rows.Next() {
					if err := rows.Scan(&id, &col1, &col2, &col3, &col4); err != nil {
						log.Fatal(err)
					}
				}
				atomic.AddInt64(&cost, time.Since(start).Nanoseconds())
				atomic.AddInt64(&qps, 1)
			}
		}()
	}

	var q int64 = 0
	for {
		fmt.Println("qps =", qps-q, "latency =", cost/(qps-q)/1000)
		q = atomic.LoadInt64(&qps)
		atomic.StoreInt64(&cost, 0)
		time.Sleep(1 * time.Second)
	}
}
