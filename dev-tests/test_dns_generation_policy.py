from __future__ import annotations

import re
import unittest
from pathlib import Path


GENERATOR = Path(__file__).parents[1] / "src" / "configs" / "generate.cpp"


class DNSGenerationPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        source = GENERATOR.read_text(encoding="utf-8")
        match = re.search(
            r"void buildDNSSection\(.*?(?=// -+ inbounds)",
            source,
            flags=re.DOTALL,
        )
        assert match is not None
        cls.dns_builder = match.group(0)

    def test_legacy_strategy_is_not_emitted_on_dns_rule_actions(self) -> None:
        self.assertNotIn('{"strategy",', self.dns_builder)

    def test_strategy_is_applied_at_dns_client_level(self) -> None:
        self.assertIn('dnsObj["strategy"] = dnsStrategy', self.dns_builder)


if __name__ == "__main__":
    unittest.main()
