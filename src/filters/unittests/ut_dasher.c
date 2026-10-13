#include "tests.h"
#include <gpac/filters.h>

Bool dasher_scte35_boundary_due(u64 packet_cts, u32 packet_timescale, u32 sap_type, u64 boundary_time);

unittest(dasher_scte35_boundary_uses_first_sap_at_or_after_splice)
{
	const u64 splice_time_90k = 6330602;

	/* A predictive packet after the cue cannot start an independently decodable
	 * HLS segment. */
	assert_false(dasher_scte35_boundary_due(6332400, 90000, GF_FILTER_SAP_NONE, splice_time_90k));

	/* A SAP before the requested splice point is still too early. */
	assert_false(dasher_scte35_boundary_due(6330000, 90000, GF_FILTER_SAP_1, splice_time_90k));

	/* Exact and slightly-later SAPs are valid boundaries. The latter models the
	 * real broadcast sample where the cue is about 20 ms before the IDR. */
	assert_true(dasher_scte35_boundary_due(6330602, 90000, GF_FILTER_SAP_1, splice_time_90k));
	assert_true(dasher_scte35_boundary_due(6332400, 90000, GF_FILTER_SAP_1, splice_time_90k));

	/* Comparison is timescale-safe. 70.36 s in a 1 kHz stream is after the
	 * 70.340022 s SCTE splice time. */
	assert_true(dasher_scte35_boundary_due(70360, 1000, GF_FILTER_SAP_1, splice_time_90k));
	assert_false(dasher_scte35_boundary_due(70300, 1000, GF_FILTER_SAP_1, splice_time_90k));
}

/* Duplicate SCTE events must schedule every visual PID, not only the first. */
Bool dasher_scte35_schedule_boundary(Bool, u32, u64, u64, u64 *, u64 *, u8 *);
unittest(dasher_scte35_duplicate_event_schedules_each_video_representation)
{
	u64 a=0, b=0, c=0, d=0, x=0, y=0;
	u8 sa=0, sb=0, sc=0;
	assert_true(dasher_scte35_schedule_boundary(GF_TRUE, GF_STREAM_VISUAL, 6330602, 21780000, &a, &b, &sa));
	assert_true(dasher_scte35_schedule_boundary(GF_TRUE, GF_STREAM_VISUAL, 6330602, 21780000, &c, &d, &sb));
	assert_equal(a, c, LLU);
	assert_equal(b, d, LLU);
	assert_equal(sa, sb, "%u");
	assert_false(dasher_scte35_schedule_boundary(GF_TRUE, GF_STREAM_VISUAL, 6330602, 21780000, &a, &b, &sa));
	assert_false(dasher_scte35_schedule_boundary(GF_TRUE, GF_STREAM_AUDIO, 6330602, 21780000, &x, &y, &sc));
}

Bool dasher_ssai_boundary_reached(u64 source_cts, u32 timescale, u64 cut90k);
unittest(dasher_ssai_common_cut_clock_and_audio_sample_tolerance)
{
	const u64 cut = 6332400;
	assert_false(dasher_ssai_boundary_reached(6332399, 90000, cut));
	assert_true(dasher_ssai_boundary_reached(6332400, 90000, cut));
	assert_true(dasher_ssai_boundary_reached(3377280, 48000, cut));
	assert_false(dasher_ssai_boundary_reached(3377279, 48000, cut));
/* Kept visible with GF_STATIC specifically for source-level regression tests. */
u64 dasher_scte35_translate_event(u64, u32, u64, u64, s64, u64, Bool, u32);
Bool dasher_scte35_transition_at_segment(u64, u64, Bool, u64);
}

unittest(dasher_scte35_f16_independent_rendition_origins)
{
    /* F16 source SCTE event PTS, independent video/audio media origins.
     * Both next physical boundaries follow the same 90k event. */
    const u64 event90k = 8076794552ULL;
    const u64 vstart = 8076796350ULL;
    const u64 astart = 8076797417ULL;
    const u64 vfirst = 8070463950ULL;
    const u64 afirst = 8070192137ULL;
    const u64 vevt = dasher_scte35_translate_event(event90k, 90000, vfirst, vfirst, 0, 0, GF_FALSE, 90000);
    const u64 aevt = dasher_scte35_translate_event(event90k, 90000, afirst, afirst, 0, 0, GF_FALSE, 90000);
    const u64 vnext = vstart-vfirst, anext = astart-afirst;
    assert_equal(vevt, (u64)6330602, LLU);
    assert_equal(aevt, (u64)6602415, LLU);
    assert_true(dasher_scte35_transition_at_segment(vevt, vnext, GF_TRUE, vnext-228000));
    assert_true(dasher_scte35_transition_at_segment(aevt, anext, GF_TRUE, anext-226800));
    assert_false(dasher_scte35_transition_at_segment(aevt, anext-226800, GF_TRUE, anext-453600));
}

unittest(dasher_scte35_mpd_scale_and_no_duplicate_boundary)
{
    const u64 evt = dasher_scte35_translate_event(8076794552ULL, 90000,
        8070192137ULL, 8070192137ULL, 0, 0, GF_FALSE, 1000);
    assert_equal(evt, (u64)73360, LLU);
    assert_true(dasher_scte35_transition_at_segment(100, 100, GF_TRUE, 90));
    assert_false(dasher_scte35_transition_at_segment(100, 101, GF_TRUE, 100));
    assert_true(dasher_scte35_transition_at_segment(10, 12, GF_FALSE, 0));
}
