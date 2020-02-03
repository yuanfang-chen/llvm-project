"""
Test discovery functions.
"""

import copy
import os
import sys

from lit.TestingConfig import TestingConfig
from lit import LitConfig, Test

def chooseConfigFileFromDir(dir, config_names):
    for name in config_names:
        p = os.path.join(dir, name)
        if os.path.exists(p):
            return p
    return None

def dirContainsTestSuite(path, lit_config):
    cfgpath = chooseConfigFileFromDir(path, lit_config.site_config_names)
    if not cfgpath:
        cfgpath = chooseConfigFileFromDir(path, lit_config.config_names)
    return cfgpath

def getTestSuite(item, litConfig, testSuiteCache, testConfigCache):
    """getTestSuite(item, litConfig, testSuiteCache,
                    testConfigCache) -> (suite, relative_path)

    Find the test suite containing @arg item.

    @retval (None, ...) - Indicates no test suite contains @arg item.
    @retval (suite, relative_path) - The suite that @arg item is in, and its
    relative path inside that suite.
    """
    def search1(path):
        # Check for a site config or a lit config.
        cfgpath = dirContainsTestSuite(path, litConfig)

        # If we didn't find a config file, keep looking.
        if not cfgpath:
            parent,base = os.path.split(path)
            if parent == path:
                return (None, ())

            ts, relative = search(parent)
            return (ts, relative + (base,))

        # This is a private builtin parameter which can be used to perform
        # translation of configuration paths.  Specifically, this parameter
        # can be set to a dictionary that the discovery process will consult
        # when it finds a configuration it is about to load.  If the given
        # path is in the map, the value of that key is a path to the
        # configuration to load instead.
        config_map = litConfig.params.get('config_map')
        if config_map:
            cfgpath = os.path.realpath(cfgpath)
            cfgpath = os.path.normcase(cfgpath)
            target = config_map.get(cfgpath)
            if target:
                cfgpath = target

        # Read multi-cfg test suite
        cfgpaths = []
        ts_multi = any(cfgpath.endswith(s) for s in litConfig.multiconfig_suffixes)
        if ts_multi:
            f = open(cfgpath)
            ts_name = next(f).rstrip()
            try:
                for ln in f:
                    ln = ln.strip()
                    if ln:
                        cfgpaths.append(ln)
            finally:
                f.close()
        else:
            cfgpaths.append(cfgpath)

        cfgs = []
        for cp in cfgpaths:
            cp = os.path.realpath(cp)
            cp = os.path.normpath(cp)
            cfg = testConfigCache.get(cp)
            if cfg is None:
                # We found a test suite config, create a TestingConfig and load it.
                if litConfig.debug:
                    litConfig.note('loading suite config %r' % cp)
                cfg = TestingConfig.fromdefaults(litConfig)
                cfg.load_from_path(cp, litConfig)
                cfg.test_exec_root = os.path.realpath(cfg.test_exec_root or path)
                cfg.test_source_root = os.path.realpath(cfg.test_source_root or path)
                testConfigCache[cp] = cfg
            cfgs.append(cfg)

            if not ts_multi:
                ts_name = cfg.name

        return Test.TestSuite(ts_name, cfgs, ts_multi), ()

    def search(path):
        # Check for an already instantiated test suite.
        real_path = os.path.realpath(path)
        res = testSuiteCache.get(real_path)
        if res is None:
            testSuiteCache[real_path] = res = search1(path)
        return res

    # Canonicalize the path.
    item = os.path.normpath(os.path.join(os.getcwd(), item))

    # Skip files and virtual components.
    components = []
    while not os.path.isdir(item):
        parent,base = os.path.split(item)
        if parent == item:
            return (None, ())
        components.append(base)
        item = parent
    components.reverse()

    ts, relative = search(item)
    return ts, tuple(relative + tuple(components))

def getLocalConfig(tc, path_in_suite, litConfig, cache):
    def search1(path_in_suite):
        # Get the parent config.
        if not path_in_suite:
            parent = tc
        else:
            parent = search(path_in_suite[:-1])

        # Check if there is a local configuration file.
        source_path = tc.getSourcePath(path_in_suite)
        cfgpath = chooseConfigFileFromDir(source_path, litConfig.local_config_names)

        # If not, just reuse the parent config.
        if not cfgpath:
            return parent

        # Otherwise, copy the current config and load the local configuration
        # file into it.
        config = copy.deepcopy(parent)
        if litConfig.debug:
            litConfig.note('loading local config %r' % cfgpath)
        config.load_from_path(cfgpath, litConfig)
        # config without parent is a test suite config, not a local config
        if config.parent is None:
            config.parent = parent
        return config

    def search(path_in_suite):
        key = (tc, path_in_suite)
        res = cache.get(key)
        if res is None:
            cache[key] = res = search1(path_in_suite)
        return res

    return search(path_in_suite)

def getTests(path, litConfig, testSuiteCache, testConfigCache, localConfigCache):
    # Find the test suite for this input and its relative path.
    ts,path_in_suite = getTestSuite(path, litConfig,
                                    testSuiteCache, testConfigCache)
    if ts is None:
        litConfig.warning('unable to find test suite for %r' % path)
        return (),()

    tests = []
    for tc in ts.configs:
        if litConfig.debug:
            litConfig.note('resolved input %r to %r::%r' % (path, tc.name,
                                                            path_in_suite))
        tests.extend(getTestsInSuite(ts, tc, path_in_suite, litConfig,
                                     testSuiteCache, testConfigCache,
                                     localConfigCache))
    return tests

def getTestsInSuite(ts, tc, path_in_suite, litConfig,
                    testSuiteCache, testConfigCache, localConfigCache):
    # Check that the source path exists (errors here are reported by the
    # caller).
    source_path = tc.getSourcePath(path_in_suite)
    if not os.path.exists(source_path):
        return

    # Check if the user named a test directly.
    if not os.path.isdir(source_path):
        lc = getLocalConfig(tc, path_in_suite[:-1], litConfig, localConfigCache)
        yield Test.Test(ts, path_in_suite, lc)
        return

    # Otherwise we have a directory to search for tests, start by getting the
    # local configuration.
    lc = getLocalConfig(tc, path_in_suite, litConfig, localConfigCache)

    # Search for tests.
    if lc.test_format is not None:
        for res in lc.test_format.getTestsInDirectory(ts, path_in_suite,
                                                      litConfig, lc):
            yield res

    # Search subdirectories.
    for filename in os.listdir(source_path):
        # FIXME: This doesn't belong here?
        if filename in ('Output', '.svn', '.git') or filename in lc.excludes:
            continue

        # Ignore non-directories.
        file_sourcepath = os.path.join(source_path, filename)
        if not os.path.isdir(file_sourcepath):
            continue

        # Check for nested test suites, first in the execpath in case there is a
        # site configuration and then in the source path.
        subpath = path_in_suite + (filename,)
        file_execpath = tc.getExecPath(subpath)
        if dirContainsTestSuite(file_execpath, litConfig):
            sub_ts, subpath_in_suite = getTestSuite(file_execpath, litConfig,
                                                    testSuiteCache, testConfigCache)
        elif dirContainsTestSuite(file_sourcepath, litConfig):
            sub_ts, subpath_in_suite = getTestSuite(file_sourcepath, litConfig,
                                                    testSuiteCache, testConfigCache)
        else:
            sub_ts = None

        # If the this directory recursively maps back to the current test suite,
        # disregard it (this can happen if the exec root is located inside the
        # current test suite, for example).
        if sub_ts is ts:
            continue

        if sub_ts is not None:
            assert not sub_ts.is_multi_cfg, 'multi cfg only happen at exec side where sub suite is not possible'
            if sub_ts.configs[0] in ts.configs:
                continue

        # Otherwise, load from the nested test suite, if present.
        subts = ts if sub_ts is None else sub_ts
        subpath = subpath if sub_ts is None else subpath_in_suite
        for sub_tc in subts.configs:
            subiter = getTestsInSuite(subts, sub_tc, subpath, litConfig,
                                      testSuiteCache, testConfigCache,
                                      localConfigCache)
            N = 0
            for res in subiter:
                N += 1
                yield res

            if sub_ts and not N:
                litConfig.warning('test suite %r contained no tests' % sub_tc.name)

def find_tests_for_inputs(lit_config, inputs):
    """
    find_tests_for_inputs(lit_config, inputs) -> [Test]

    Given a configuration object and a list of input specifiers, find all the
    tests to execute.
    """

    # Expand '@...' form in inputs.
    actual_inputs = []
    for input in inputs:
        if input.startswith('@'):
            f = open(input[1:])
            try:
                for ln in f:
                    ln = ln.strip()
                    if ln:
                        actual_inputs.append(ln)
            finally:
                f.close()
        else:
            actual_inputs.append(input)

    # Load the tests from the inputs.
    tests = []
    test_suite_cache = {}
    test_config_cache = {} # Avoid reloading the same test config.
    local_config_cache = {}
    for input in actual_inputs:
        prev = len(tests)
        tests.extend(getTests(input, lit_config, test_suite_cache,
                              test_config_cache, local_config_cache))
        if prev == len(tests):
            lit_config.warning('input %r contained no tests' % input)

    # If there were any errors during test discovery, exit now.
    if lit_config.numErrors:
        sys.stderr.write('%d errors, exiting.\n' % lit_config.numErrors)
        sys.exit(2)

    return tests
