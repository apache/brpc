# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

function(do_fetch)
  message(VERBOSE "Fetching latest from the remote origin")
  execute_process(
    COMMAND "/usr/bin/git" --git-dir=.git fetch --tags --force "origin"
    WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    COMMAND_ERROR_IS_FATAL LAST
  )
endfunction()

function(get_hash_for_ref ref out_var err_var)
  execute_process(
    COMMAND "/usr/bin/git" --git-dir=.git rev-parse "${ref}^0"
    WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    RESULT_VARIABLE error_code
    OUTPUT_VARIABLE ref_hash
    ERROR_VARIABLE error_msg
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(error_code)
    set(${out_var} "" PARENT_SCOPE)
  else()
    set(${out_var} "${ref_hash}" PARENT_SCOPE)
  endif()
  set(${err_var} "${error_msg}" PARENT_SCOPE)
endfunction()

get_hash_for_ref(HEAD head_sha error_msg)
if(head_sha STREQUAL "")
  message(FATAL_ERROR "Failed to get the hash for HEAD:\n${error_msg}")
endif()


execute_process(
  COMMAND "/usr/bin/git" --git-dir=.git show-ref "release-1.8.0"
  WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
  OUTPUT_VARIABLE show_ref_output
)
if(show_ref_output MATCHES "^[a-z0-9]+[ \\t]+refs/remotes/")
  # Given a full remote/branch-name and we know about it already. Since
  # branches can move around, we should always fetch, if permitted.
  if(can_fetch)
    do_fetch()
  endif()
  set(checkout_name "release-1.8.0")

elseif(show_ref_output MATCHES "^[a-z0-9]+[ \\t]+refs/tags/")
  # Given a tag name that we already know about. We don't know if the tag we
  # have matches the remote though (tags can move), so we should fetch. As a
  # special case to preserve backward compatibility, if we are already at the
  # same commit as the tag we hold locally, don't do a fetch and assume the tag
  # hasn't moved on the remote.
  # FIXME: We should provide an option to always fetch for this case
  get_hash_for_ref("release-1.8.0" tag_sha error_msg)
  if(tag_sha STREQUAL head_sha)
    message(VERBOSE "Already at requested tag: ${tag_sha}")
    return()
  endif()

  if(can_fetch)
    do_fetch()
  endif()
  set(checkout_name "release-1.8.0")

elseif(show_ref_output MATCHES "^[a-z0-9]+[ \\t]+refs/heads/")
  # Given a branch name without any remote and we already have a branch by that
  # name. We might already have that branch checked out or it might be a
  # different branch. It isn't fully safe to use a bare branch name without the
  # remote, so do a fetch (if allowed) and replace the ref with one that
  # includes the remote.
  if(can_fetch)
    do_fetch()
  endif()
  set(checkout_name "origin/release-1.8.0")

else()
  get_hash_for_ref("release-1.8.0" tag_sha error_msg)
  if(tag_sha STREQUAL head_sha)
    # Have the right commit checked out already
    message(VERBOSE "Already at requested ref: ${tag_sha}")
    return()

  elseif(tag_sha STREQUAL "")
    # We don't know about this ref yet, so we have no choice but to fetch.
    if(NOT can_fetch)
      message(FATAL_ERROR
        "Requested git ref \"release-1.8.0\" is not present locally, and not "
        "allowed to contact remote due to UPDATE_DISCONNECTED setting."
      )
    endif()

    # We deliberately swallow any error message at the default log level
    # because it can be confusing for users to see a failed git command.
    # That failure is being handled here, so it isn't an error.
    if(NOT error_msg STREQUAL "")
      message(VERBOSE "${error_msg}")
    endif()
    do_fetch()
    set(checkout_name "release-1.8.0")

  else()
    # We have the commit, so we know we were asked to find a commit hash
    # (otherwise it would have been handled further above), but we don't
    # have that commit checked out yet. We don't need to fetch from the remote.
    set(checkout_name "release-1.8.0")
    if(NOT error_msg STREQUAL "")
      message(WARNING "${error_msg}")
    endif()

  endif()
endif()

set(git_update_strategy "REBASE")
if(git_update_strategy STREQUAL "")
  # Backward compatibility requires REBASE as the default behavior
  set(git_update_strategy REBASE)
endif()

if(git_update_strategy MATCHES "^REBASE(_CHECKOUT)?$")
  # Asked to potentially try to rebase first, maybe with fallback to checkout.
  # We can't if we aren't already on a branch and we shouldn't if that local
  # branch isn't tracking the one we want to checkout.
  execute_process(
    COMMAND "/usr/bin/git" --git-dir=.git symbolic-ref -q HEAD
    WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    OUTPUT_VARIABLE current_branch
    OUTPUT_STRIP_TRAILING_WHITESPACE
    # Don't test for an error. If this isn't a branch, we get a non-zero error
    # code but empty output.
  )

  if(current_branch STREQUAL "")
    # Not on a branch, checkout is the only sensible option since any rebase
    # would always fail (and backward compatibility requires us to checkout in
    # this situation)
    set(git_update_strategy CHECKOUT)

  else()
    execute_process(
      COMMAND "/usr/bin/git" --git-dir=.git for-each-ref "--format=%(upstream:short)" "${current_branch}"
      WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
      OUTPUT_VARIABLE upstream_branch
      OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY  # There is no error if no upstream is set
    )
    if(NOT upstream_branch STREQUAL checkout_name)
      # Not safe to rebase when asked to checkout a different branch to the one
      # we are tracking. If we did rebase, we could end up with arbitrary
      # commits added to the ref we were asked to checkout if the current local
      # branch happens to be able to rebase onto the target branch. There would
      # be no error message and the user wouldn't know this was occurring.
      set(git_update_strategy CHECKOUT)
    endif()

  endif()
elseif(NOT git_update_strategy STREQUAL "CHECKOUT")
  message(FATAL_ERROR "Unsupported git update strategy: ${git_update_strategy}")
endif()


# Check if stash is needed
execute_process(
  COMMAND "/usr/bin/git" --git-dir=.git status --porcelain
  WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
  RESULT_VARIABLE error_code
  OUTPUT_VARIABLE repo_status
)
if(error_code)
  message(FATAL_ERROR "Failed to get the status")
endif()
string(LENGTH "${repo_status}" need_stash)

# If not in clean state, stash changes in order to be able to perform a
# rebase or checkout without losing those changes permanently
if(need_stash)
  execute_process(
    COMMAND "/usr/bin/git" --git-dir=.git stash save --quiet;--include-untracked
    WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    COMMAND_ERROR_IS_FATAL ANY
  )
endif()

if(git_update_strategy STREQUAL "CHECKOUT")
  execute_process(
    COMMAND "/usr/bin/git" --git-dir=.git checkout "${checkout_name}"
    WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    COMMAND_ERROR_IS_FATAL ANY
  )
else()
  execute_process(
    COMMAND "/usr/bin/git" --git-dir=.git rebase "${checkout_name}"
    WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    RESULT_VARIABLE error_code
    OUTPUT_VARIABLE rebase_output
    ERROR_VARIABLE  rebase_output
  )
  if(error_code)
    # Rebase failed, undo the rebase attempt before continuing
    execute_process(
      COMMAND "/usr/bin/git" --git-dir=.git rebase --abort
      WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    )

    if(NOT git_update_strategy STREQUAL "REBASE_CHECKOUT")
      # Not allowed to do a checkout as a fallback, so cannot proceed
      if(need_stash)
        execute_process(
          COMMAND "/usr/bin/git" --git-dir=.git stash pop --index --quiet
          WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
          )
      endif()
      message(FATAL_ERROR "\nFailed to rebase in: '/home/mike/桌面/brpc/test/googletest-src'."
                          "\nOutput from the attempted rebase follows:"
                          "\n${rebase_output}"
                          "\n\nYou will have to resolve the conflicts manually")
    endif()

    # Fall back to checkout. We create an annotated tag so that the user
    # can manually inspect the situation and revert if required.
    # We can't log the failed rebase output because MSVC sees it and
    # intervenes, causing the build to fail even though it completes.
    # Write it to a file instead.
    string(TIMESTAMP tag_timestamp "%Y%m%dT%H%M%S" UTC)
    set(tag_name _cmake_ExternalProject_moved_from_here_${tag_timestamp}Z)
    set(error_log_file ${CMAKE_CURRENT_LIST_DIR}/rebase_error_${tag_timestamp}Z.log)
    file(WRITE ${error_log_file} "${rebase_output}")
    message(WARNING "Rebase failed, output has been saved to ${error_log_file}"
                    "\nFalling back to checkout, previous commit tagged as ${tag_name}")
    execute_process(
      COMMAND "/usr/bin/git" --git-dir=.git tag -a
              -m "ExternalProject attempting to move from here to ${checkout_name}"
              ${tag_name}
      WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
      COMMAND_ERROR_IS_FATAL ANY
    )

    execute_process(
      COMMAND "/usr/bin/git" --git-dir=.git checkout "${checkout_name}"
      WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
      COMMAND_ERROR_IS_FATAL ANY
    )
  endif()
endif()

if(need_stash)
  # Put back the stashed changes
  execute_process(
    COMMAND "/usr/bin/git" --git-dir=.git stash pop --index --quiet
    WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    RESULT_VARIABLE error_code
    )
  if(error_code)
    # Stash pop --index failed: Try again dropping the index
    execute_process(
      COMMAND "/usr/bin/git" --git-dir=.git reset --hard --quiet
      WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    )
    execute_process(
      COMMAND "/usr/bin/git" --git-dir=.git stash pop --quiet
      WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
      RESULT_VARIABLE error_code
    )
    if(error_code)
      # Stash pop failed: Restore previous state.
      execute_process(
        COMMAND "/usr/bin/git" --git-dir=.git reset --hard --quiet ${head_sha}
        WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
      )
      execute_process(
        COMMAND "/usr/bin/git" --git-dir=.git stash pop --index --quiet
        WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
      )
      message(FATAL_ERROR "\nFailed to unstash changes in: '/home/mike/桌面/brpc/test/googletest-src'."
                          "\nYou will have to resolve the conflicts manually")
    endif()
  endif()
endif()

set(init_submodules "TRUE")
if(init_submodules)
  execute_process(
    COMMAND "/usr/bin/git"
            --git-dir=.git 
            submodule update --recursive --init 
    WORKING_DIRECTORY "/home/mike/桌面/brpc/test/googletest-src"
    COMMAND_ERROR_IS_FATAL ANY
  )
endif()
