/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef ELOQ_MODULE_H
#define ELOQ_MODULE_H

namespace eloq {
    class EloqModule {
    public:
        virtual ~EloqModule() {
        }

        /**
         * This func is called when worker starts running.
         * @param thd_id
         */
        virtual void ExtThdStart(int thd_id) = 0;

        /**
         * Called when worker stop running and sleep.
         * @param thd_id
         */
        virtual void ExtThdEnd(int thd_id) = 0;

        /**
         * How the module task is processed.
         * @param thd_id
         */
        virtual void Process(int thd_id) = 0;

        /**
         *
         * @param thd_id
         * @return whther the module has task to process.
         */
        virtual bool HasTask(int thd_id) const = 0;

        /**
         * This func is for the module to wakeup the worker.
         * @param thd_id
         * @return true if the worker is running or successfully notified.
         */
        static bool NotifyWorker(int thd_id);
    };

    extern int register_module(EloqModule *module);

    extern int unregister_module(EloqModule *module);
} // namespace eloq

#endif //ELOQ_MODULE_H
