#!/usr/bin/env python3
"""
Regression tests for find-stream's pure logic.

These cover the parsing and candidate-generation bugs found while building the
tool. They do not touch the network: every case here is decided without
decoding, so the suite stays fast and cannot report a live station as working.

Run:  python3 tests/test_find_stream.py
"""
import importlib.util
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, os.pardir, "tools", "find-stream")


def load_tool():
    with open(TOOL) as fh:
        src = fh.read().replace(
            'if __name__ == "__main__":\n    sys.exit(main())', "")
    mod = type(sys)("find_stream_under_test")
    mod.__dict__["__name__"] = "find_stream_under_test"
    exec(compile(src, TOOL, "exec"), mod.__dict__)
    return mod


fs = load_tool()


class TestNormalizeCallsign(unittest.TestCase):
    """Underscores are significant and must survive normalisation."""

    def test_preserves_underscore(self):
        # Regression: stripping this produced CBLAFMCBC, which 404s.
        self.assertEqual(fs.normalize_callsign("CBLAFM_CBC"), "CBLAFM_CBC")

    def test_preserves_underscore_with_mixed_case(self):
        self.assertEqual(fs.normalize_callsign("cbla_fm_cbc"), "CBLA_FM_CBC")

    def test_uppercases(self):
        self.assertEqual(fs.normalize_callsign("chomfm"), "CHOMFM")

    def test_drops_dashes_and_spaces(self):
        self.assertEqual(fs.normalize_callsign("CH-OM FM"), "CHOMFM")

    def test_drops_dots(self):
        self.assertEqual(fs.normalize_callsign("CHOM.FM"), "CHOMFM")

    def test_keeps_digits(self):
        self.assertEqual(fs.normalize_callsign("cfrb1010"), "CFRB1010")

    def test_empty_for_pure_punctuation(self):
        self.assertEqual(fs.normalize_callsign("---"), "")


class TestExtractPls(unittest.TestCase):
    """A PLS lists several mirrors; every one is a candidate."""

    def test_takes_every_file_entry(self):
        body = (
            b"[playlist]\n"
            b"File1=https://a.example/one\n"
            b"File2=https://b.example/two\n"
            b"Title1=One\n"
            b"NumberOfEntries=2\n"
            b"Version=2\n"
        )
        self.assertEqual(fs.extract_pls_all(body),
                         ["https://a.example/one", "https://b.example/two"])

    def test_skips_empty_file_entries(self):
        # CFMB-style: a 200 response whose File1 is blank must not be trusted.
        body = b"[playlist]\nFile1=\nFile2=https://b.example/two\n"
        self.assertEqual(fs.extract_pls_all(body), ["https://b.example/two"])

    def test_ignores_non_file_lines(self):
        body = b"[playlist]\nLength1=-1\nFile1=https://a.example/one\nVersion=2\n"
        self.assertEqual(fs.extract_pls_all(body), ["https://a.example/one"])

    def test_empty_body_yields_nothing(self):
        self.assertEqual(fs.extract_pls_all(b""), [])

    def test_case_insensitive_key(self):
        body = b"[playlist]\nfile1=https://a.example/one\n"
        self.assertEqual(fs.extract_pls_all(body), ["https://a.example/one"])


class TestCandidateGeneration(unittest.TestCase):
    """The generated URL set must actually contain the path that works."""

    def test_bare_callsign_is_a_candidate(self):
        urls = [u for _t, u in fs.streamtheworld_candidates("CHOMFM")]
        self.assertIn(
            "https://playerservices.streamtheworld.com/pls/CHOMFM.pls", urls)

    def test_underscored_callsign_yields_working_path(self):
        # Regression: the exact PLS that serves CBC Radio One Toronto.
        urls = [u for _t, u in fs.streamtheworld_candidates("CBLAFM_CBC")]
        self.assertIn(
            "https://playerservices.streamtheworld.com/pls/CBLAFM_CBC.pls", urls)

    def test_suffix_variants_generated(self):
        urls = [u for _t, u in fs.streamtheworld_candidates("CHOMFM")]
        # The HLS adaptive variant is hyphenated: CHOMFM-ADP.pls.
        self.assertIn(
            "https://playerservices.streamtheworld.com/pls/CHOMFM-ADP.pls", urls)

    def test_leanstream_includes_mp3_fallback(self):
        # The -MP3 sibling is what rescues stations whose AAC path is dead.
        urls = [u for _t, u in fs.leanstream_candidates("CIMJFM")]
        self.assertIn("https://live.leanstream.co/CIMJFM-MP3", urls)

    def test_leanstream_preserves_underscore(self):
        urls = [u for _t, u in fs.leanstream_candidates("CJOB_AM")]
        self.assertTrue(any("CJOB_AM" in u for u in urls))

    def test_empty_callsign_yields_no_candidates(self):
        self.assertEqual(list(fs.streamtheworld_candidates("...")), [])
        self.assertEqual(list(fs.leanstream_candidates("...")), [])


class TestIsMalformedMaster(unittest.TestCase):
    """
    The discriminator that decides whether a leanstream master is broken.
    Blindly rewriting a healthy station breaks it, so this test guards the
    distinction that the runtime repair also depends on.
    """

    def test_bare_tag_is_malformed(self):
        self.assertTrue(fs.is_malformed_master(
            "#EXTM3U\n#EXT-X-STREAM-INF:\nhttp://host/x.stream/playlist.m3u8\n"))

    def test_valid_master_is_not_malformed(self):
        self.assertFalse(fs.is_malformed_master(
            "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=47000,CODECS=\"mp4a.40.5\"\n"
            "https://host/x.stream/48k/playlist.m3u8\n"))

    def test_media_playlist_is_not_malformed(self):
        # A media playlist has no STREAM-INF at all.
        self.assertFalse(fs.is_malformed_master(
            "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6.0,\nseg1.aac\n"))

    def test_non_playlist_is_not_malformed(self):
        self.assertFalse(fs.is_malformed_master(b"binary audio data"))


class TestSameStation(unittest.TestCase):
    """
    Guard against substituting a different station that happens to decode.

    Regression: querying "TED" matched "A Dick Ted Radio" and
    "Addicted 2 Oldies Music Radio" on a substring, and the tool reported a
    Jamendo stream as if it were the station. The stream played, so the decode
    test passed, and the answer was still wrong.
    """

    def test_rejects_substring_common_word(self):
        self.assertFalse(fs.same_station("A Dick Ted Radio", "TED"))
        self.assertFalse(fs.same_station(
            "Addicted 2 Oldies Music Radio Canada's #1", "TED"))

    def test_accepts_exact_call_sign_match(self):
        self.assertTrue(fs.same_station("CFRB News/Talk 1010", "CFRB"))
        self.assertTrue(fs.same_station("CHOM 97.7", "CHOMFM"))

    def test_rejects_different_call_sign(self):
        self.assertFalse(fs.same_station("CFMB 1280", "CHOM"))
        self.assertFalse(fs.same_station("CHMJ 900", "CFRB"))

    def test_frequency_mismatch_rejected(self):
        # Same brand, different frequency, different station.
        self.assertFalse(fs.same_station("CITE 92.1 Winnipeg", "CITE 91.1 Toronto"))

    def test_frequency_match_accepted(self):
        self.assertTrue(fs.same_station("CHOM 97.7 Toronto", "CHOM 97.7"))

    def test_rejects_when_query_is_single_generic_word(self):
        self.assertFalse(fs.same_station("Some Radio Station", "Radio"))
        self.assertFalse(fs.same_station("Classic Rock Channel", "Rock"))

    def test_empty_inputs_rejected(self):
        self.assertFalse(fs.same_station("", "CHOM"))
        self.assertFalse(fs.same_station("CHOM 97.7", ""))

    def test_multiword_query_needs_substantial_overlap(self):
        self.assertTrue(fs.same_station(
            "CityNews 570 Kitchener", "CityNews 570 Kitchener"))
        self.assertFalse(fs.same_station(
            "CBC Radio One Toronto", "CityNews 680 Toronto"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
