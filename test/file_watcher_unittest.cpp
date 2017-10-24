#include <gtest/gtest.h>
#include "butil/files/file_watcher.h"
#include "butil/logging.h"

namespace {
class FileWatcherTest : public ::testing::Test{
protected:
    FileWatcherTest(){};
    virtual ~FileWatcherTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};
 
//! gejun: check basic functions of butil::FileWatcher
TEST_F(FileWatcherTest, random_op) {
    srand (time(0));
    
    butil::FileWatcher fw;
    EXPECT_EQ (0, fw.init("dummy_file"));
    
    for (int i=0; i<30; ++i) {
        if (rand() % 2) {
            const butil::FileWatcher::Change ret = fw.check_and_consume();
            switch (ret) {
            case butil::FileWatcher::UPDATED:
                LOG(INFO) << fw.filepath() << " is updated";
                break;
            case butil::FileWatcher::CREATED:
                LOG(INFO) << fw.filepath() << " is created";
                break;
            case butil::FileWatcher::DELETED:
                LOG(INFO) << fw.filepath() << " is deleted";
                break;
            case butil::FileWatcher::UNCHANGED:
                LOG(INFO) << fw.filepath() << " does not change or still not exist";
                break;
            }
        }
        
        switch (rand() % 2) {
        case 0:
            ASSERT_EQ(0, system("touch dummy_file"));
            LOG(INFO) << "action: touch dummy_file";
            break;
        case 1:
            ASSERT_EQ(0, system("rm -f dummy_file"));
            LOG(INFO) << "action: rm -f dummy_file";
            break;
        case 2:
            LOG(INFO) << "action: (nothing)";
            break;
        }
        
        usleep (10000);
    }
    ASSERT_EQ(0, system("rm -f dummy_file"));
}

}  // namespace
