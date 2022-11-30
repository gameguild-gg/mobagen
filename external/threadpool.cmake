CPMAddPackage(
        NAME thread-pool
        GITHUB_REPOSITORY DeveloperPaul123/thread-pool
        GIT_TAG 0.5.1
        OPTIONS "TP_BUILD_TESTS OFF" "TP_BUILD_EXAMPLES OFF" "TP_BUILD_BENCHMARKS OFF"
)
#find_package(dp::thread-pool REQUIRED)
# todo: test https://github.com/Ahajha/LMRTFY
# todo: test thread pool functionality