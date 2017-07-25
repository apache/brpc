#include <gtest/gtest.h>
#include "base/files/file_watcher.h"
#include "base/logging.h"

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
 
//! gejun: check basic functions of base::FileWatcher
TEST_F(FileWatcherTest, random_op)
{
    srand (time(0));
    
    base::FileWatcher fw;
    EXPECT_EQ (0, fw.init("dummy_file"));
    
    for (int i=0; i<30; ++i) {
        if (rand() % 2) {
            LOG(INFO) << "watch: " << noflush;
            const base::FileWatcher::Change ret = fw.check_and_consume();
            switch (ret) {
            case base::FileWatcher::UPDATED:
                LOG(INFO) << fw.filepath() << " is updated";
                break;
            case base::FileWatcher::CREATED:
                LOG(INFO) << fw.filepath() << " is created";
                break;
            case base::FileWatcher::DELETED:
                LOG(INFO) << fw.filepath() << " is deleted";
                break;
            case base::FileWatcher::UNCHANGED:
                LOG(INFO) << fw.filepath() << " does not change or still not exist";
                break;
            }
            LOG(INFO);
        }
        
        switch (rand() % 2) {
        case 0:
            system ("touch dummy_file");
            LOG(INFO) << "action: touch dummy_file";
            break;
        case 1:
            system ("rm -f dummy_file");
            LOG(INFO) << "action: rm -f dummy_file";
            break;
        case 2:
            LOG(INFO) << "action: (nothing)";
            break;
        }
        
        usleep (10000);
    }
    system ("rm -f dummy_file");
}
}  
