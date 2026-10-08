"""Exercise real CMake registration and CTest label intersection end to end."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('scope_do', ROOT / 'do.py')
do = importlib.util.module_from_spec(spec)
# dataclasses in do.py resolve their module through sys.modules.
import sys
sys.modules[spec.name] = do
spec.loader.exec_module(do)


class TestScopeIntegration(unittest.TestCase):
    def test_build_membership_and_behavioral_selection(self):
        with tempfile.TemporaryDirectory(prefix='draxul-scopes-') as directory:
            build = Path(directory)
            def configure(enabled):
                subprocess.run(['cmake', '-S', str(ROOT / 'tests/cmake/test_scopes'),
                                '-B', str(build), f'-DDRAXUL_SOURCE_ROOT={ROOT}',
                                f'-DENABLE_PRODUCT={enabled}'],
                               check=True, capture_output=True, text=True)
            def selected(products=set(), all_tests=False, label=None):
                _, filters, _ = do._test_scope_selection(products, all_tests, label)
                # Like do.py, name a configuration: CTest lists no tests from a
                # multi-config tree (the default Visual Studio generator) without one.
                result = subprocess.run(['ctest', '--test-dir', str(build),
                                         '--build-config', 'Debug',
                                         *filters, '--show-only=json-v1'],
                                        check=True, capture_output=True, text=True)
                return {test['name'] for test in json.loads(result.stdout)['tests']}
            configure('ON')
            core = {'test-app-shell', 'test-host-api', 'test-server', 'script-integration'}
            product = {'test-megacity-model', 'test-megacity-parser'}
            self.assertEqual(core, selected())
            self.assertEqual(core | product, selected({'megacity'}))
            self.assertEqual(core, selected({'megacity'}, label='integration'))
            self.assertEqual({'test-app-shell'}, selected(label='app-shell'))
            self.assertEqual(set(), selected(label='megacity'))
            self.assertEqual(set(), selected(label='nonexistent'))
            self.assertEqual(core | product, selected(all_tests=True))
            configure('OFF')
            self.assertEqual(core, selected())
            self.assertEqual(core, selected({'megacity'}))
            self.assertEqual(core, selected(all_tests=True))


if __name__ == '__main__':
    unittest.main()
