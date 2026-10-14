#include "tests.h"
#include "../mpd.c"

// functions used by mpd.c, needed for linking
#include "../m3u8.c"
void gf_xml_dump_string(FILE* file, const char *before, const char *str, const char *after) {
	if (before) gf_fprintf(file, "%s", before);
	if (str) {
		for (const char *p = str; *p; p++) {
			switch (*p) {
			case '&': gf_fprintf(file, "&amp;"); break;
			case '<': gf_fprintf(file, "&lt;"); break;
			case '>': gf_fprintf(file, "&gt;"); break;
			case '\'': gf_fprintf(file, "&apos;"); break;
			case '"': gf_fprintf(file, "&quot;"); break;
			default: gf_fprintf(file, "%c", *p); break;
			}
		}
	}
	if (after) gf_fprintf(file, "%s", after);
}

/*************************************/

static Bool hls_output_contains(FILE *output, const char *marker)
{
	char text[4096];
	size_t bytes_read;

	fflush(output);
	rewind(output);
	memset(text, 0, sizeof(text));
	bytes_read = fread(text, 1, sizeof(text) - 1, output);
	text[bytes_read] = 0;
	return strstr(text, marker) ? GF_TRUE : GF_FALSE;
}

unittest(mpd_hls_scte35_dynamic_rewrite_preserves_cues)
{
	GF_MPD mpd = {0};
	GF_MPD_Period period = {0};
	GF_MPD_AdaptationSet adaptation_set = {0};
	GF_MPD_Representation representation = {0};
	GF_DASH_SegmentContext segment = {0};
	GF_MPD_EventStream event_stream = {0};
	GF_MPD_EventStreamEntry event_entry = {0};
	FILE *first_playlist;
	FILE *second_playlist;
	GF_Err error;

	period.event_streams = gf_list_new();
	event_stream.entries = gf_list_new();
	representation.state_seg_list = gf_list_new();
	assert_true(period.event_streams != NULL);
	assert_true(event_stream.entries != NULL);
	assert_true(representation.state_seg_list != NULL);

	mpd.type = GF_MPD_TYPE_DYNAMIC;
	representation.timescale = 1000;
	representation.hls_max_seg_dur.num = 6;
	representation.hls_max_seg_dur.den = 1;

	segment.time = 0;
	segment.dur = 6000;
	segment.seg_num = 1;
	segment.filename = "segment1.m4s";
	gf_list_add(representation.state_seg_list, &segment);

	event_stream.timescale = 1000;
	event_entry.presentation_time = 0;
	event_entry.duration = 20000;
	event_entry.id = 662;
	gf_list_add(event_stream.entries, &event_entry);
	gf_list_add(period.event_streams, &event_stream);

	first_playlist = tmpfile();
	assert_true(first_playlist != NULL);
	error = gf_mpd_write_m3u8_playlist(&mpd, &period, &adaptation_set, &representation,
		"first.m3u8", 7, 0, NULL, GF_FALSE, GF_FALSE, first_playlist);
	assert_true(error == GF_OK);
	assert_true(hls_output_contains(first_playlist, "#EXT-X-CUE-OUT:20"));
	assert_equal(event_entry.state, 1, "%u");
	fclose(first_playlist);

	/* A dynamic playlist is rebuilt from retained segments. The same cue must be
	 * serialized again; state left by the previous write must not hide it. */
	second_playlist = tmpfile();
	assert_true(second_playlist != NULL);
	error = gf_mpd_write_m3u8_playlist(&mpd, &period, &adaptation_set, &representation,
		"second.m3u8", 7, 0, NULL, GF_FALSE, GF_FALSE, second_playlist);
	assert_true(error == GF_OK);
	assert_true(hls_output_contains(second_playlist, "#EXT-X-CUE-OUT:20"));
	assert_equal(event_entry.state, 1, "%u");
	fclose(second_playlist);

	gf_list_del(representation.state_seg_list);
	gf_list_del(event_stream.entries);
	gf_list_del(period.event_streams);
}

/*************************************/

unittest(mpd_event_streams)
{
	GF_DOMParser *dom = gf_xml_dom_new();
	GF_Err e = gf_xml_dom_parse_string(dom, "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\">\n"
". <Period id=\"1\" start=\"PT0S\">\n"
"    <EventStream schemeIdUri=\"urn:scte:scte35:2014:xml+bin\" timescale=\"1000\">\n"
"      <Event presentationTime=\"1765533611520\" duration=\"38400\"  id=\"14596012\">\n"
"       <Signal xmlns=\"http://www.scte.org/schemas/35/2016\">\n"
"          <Binary>/DAgAAAAAAAAAP/wDwUA3resf//+ADS8AMAAAAAAAORhJCQ=</Binary>\n"
"        </Signal>\n"
"      </Event>\n"
"      <Event presentationTime=\"1765533732480\" duration=\"38400\" id=\"14596013\">\n"
"        <Signal xmlns=\"http://www.scte.org/schemas/35/2016\">\n"
"          <Binary>/DAgAAAAAAAAAP/wDwUA3retf//+ADS8AMAAAAAAAORhJCQ=</Binary>\n"
"        </Signal>\n"
"      </Event>\n"
"    </EventStream>\n"
"  </Period>\n"
"</MPD>\n");
	assert_true(e == GF_OK);
	GF_MPD *mpd = gf_mpd_new();
	e = gf_mpd_init_from_dom(gf_xml_dom_get_root(dom), mpd, NULL);
	assert_true(e == GF_OK);

	GF_List *evts = ((GF_MPD_Period *)gf_list_get(mpd->periods, 0))->event_streams;
	assert_equal(gf_list_count(evts), 1, "%u");
	GF_MPD_EventStream *es = gf_list_get(evts, 0);
	assert_equal(gf_list_count(es->entries), 2, "%u");

    GF_MPD_EventStreamEntry *entry = gf_list_get(es->entries, 1);
    assert_equal(entry->presentation_time, (s64)1765533732480, LLD);
    assert_equal(entry->duration, 38400, "%u");
    assert_equal(entry->id, 14596013, "%u");
    assert_equal(entry->message_size, 35, "%u");

	{
		u32 sz = 2*entry->message_size+3;
		u8 *b64 = gf_malloc(sizeof(char)*sz);
		assert_true(b64 != NULL);
		sz = gf_base64_encode((u8*)entry->message, entry->message_size, b64, sz);
		b64[sz] = 0;
		assert_equal(sz, 48, "%u");
		assert_equal_str(b64, "/DAgAAAAAAAAAP/wDwUA3retf//+ADS8AMAAAAAAAORhJCQ=");
		gf_free(b64);
	}

	gf_xml_dom_del(dom);
	gf_mpd_del(mpd);
}

/*************************************/

unittest(mpd_hls_scte35_nonzero_presentation_time_offset)
{
	GF_MPD_Period period = {0};
	GF_MPD_AdaptationSet adaptation_set = {0};
	GF_MPD_Representation representation = {0};
	GF_MPD_SegmentTemplate segment_template = {0};
	GF_DASH_SegmentContext segment = {0};
	GF_MPD_EventStream event_stream = {0};
	GF_MPD_EventStreamEntry event = {0};

	period.event_streams = gf_list_new();
	event_stream.entries = gf_list_new();
	assert_true(period.event_streams != NULL);
	assert_true(event_stream.entries != NULL);

	segment_template.timescale = 90000;
	segment_template.presentation_time_offset = 8070463950ULL;
	adaptation_set.segment_template = &segment_template;
	representation.timescale = 90000;

	event_stream.timescale = 90000;
	event.presentation_time = 8076794552ULL;
	event.duration = 21780000U;
	event.id = 662;
	gf_list_add(event_stream.entries, &event);
	gf_list_add(period.event_streams, &event_stream);

	/* PTO-normalized event time is 6330602; CUE-OUT belongs on the first
	 * segment starting at or after that splice point. */
	segment.time = 6523200ULL;
	segment.dur = 417600U;

	FILE *output = tmpfile();
	assert_true(output != NULL);
	hls_insert_scte35_info(output, 0, &period, &adaptation_set, &representation, &segment, GF_FALSE);
	fflush(output);
	rewind(output);

	char text[1024] = {0};
	size_t bytes_read = fread(text, 1, sizeof(text)-1, output);
	text[bytes_read] = 0;
	assert_true(strstr(text, "#EXT-X-DATERANGE:ID=\"662-0000\"") == NULL);
	assert_true(strstr(text, "#EXT-X-CUE-OUT:242") != NULL);
	assert_equal(event.state, 1, "%u");

	fclose(output);
	gf_list_del(event_stream.entries);
	gf_list_del(period.event_streams);
}

/*************************************/

unittest(mpd_hls_scte35_cues_follow_segment_boundaries)
{
	GF_MPD_Period period = {0};
	GF_MPD_AdaptationSet adaptation_set = {0};
	GF_MPD_Representation representation = {0};
	GF_DASH_SegmentContext segment = {0};
	GF_MPD_EventStream event_stream = {0};
	GF_MPD_EventStreamEntry event = {0};
	char text[1024];
	size_t bytes_read;
	FILE *output;

	period.event_streams = gf_list_new();
	event_stream.entries = gf_list_new();
	assert_true(period.event_streams != NULL);
	assert_true(event_stream.entries != NULL);

	representation.timescale = 1000;
	event_stream.timescale = 1000;
	event.presentation_time = 10300;
	event.duration = 5000;
	event.id = 7;
	gf_list_add(event_stream.entries, &event);
	gf_list_add(period.event_streams, &event_stream);

	/* The splice occurs inside this segment. A cue before this segment would
	 * start replacement 300 ms too early, so no marker belongs here. */
	segment.time = 10000;
	segment.dur = 4000;
	output = tmpfile();
	assert_true(output != NULL);
	hls_insert_scte35_info(output, 0, &period, &adaptation_set, &representation, &segment, GF_FALSE);
	fflush(output);
	rewind(output);
	memset(text, 0, sizeof(text));
	bytes_read = fread(text, 1, sizeof(text)-1, output);
	text[bytes_read] = 0;
	assert_true(strstr(text, "#EXT-X-DATERANGE:ID=\"7-0000\"") != NULL);
	assert_true(strstr(text, "#EXT-X-CUE-OUT") == NULL);
	assert_equal(event.state, 0, "%u");
	fclose(output);

	/* CUE-OUT is emitted immediately before the first segment whose start is
	 * at or after the splice point. */
	segment.time = 14000;
	segment.dur = 4000;
	output = tmpfile();
	assert_true(output != NULL);
	hls_insert_scte35_info(output, 0, &period, &adaptation_set, &representation, &segment, GF_FALSE);
	fflush(output);
	rewind(output);
	memset(text, 0, sizeof(text));
	bytes_read = fread(text, 1, sizeof(text)-1, output);
	text[bytes_read] = 0;
	assert_true(strstr(text, "#EXT-X-DATERANGE:ID=\"7-0000\"") == NULL);
	assert_true(strstr(text, "#EXT-X-CUE-OUT:5") != NULL);
	assert_true(strstr(text, "#EXT-X-CUE-IN") == NULL);
	assert_equal(event.state, 1, "%u");
	fclose(output);

	/* The break ends at 15.3 s, inside the same 14-18 s segment. Do not put
	 * CUE-IN before media that still belongs to the break. */
	output = tmpfile();
	assert_true(output != NULL);
	hls_insert_scte35_info(output, 0, &period, &adaptation_set, &representation, &segment, GF_FALSE);
	fflush(output);
	rewind(output);
	memset(text, 0, sizeof(text));
	bytes_read = fread(text, 1, sizeof(text)-1, output);
	text[bytes_read] = 0;
	assert_true(strstr(text, "#EXT-X-CUE-IN") == NULL);
	assert_equal(event.state, 1, "%u");
	fclose(output);

	/* CUE-IN is emitted before the first segment starting at or after the
	 * return point. */
	segment.time = 18000;
	segment.dur = 4000;
	output = tmpfile();
	assert_true(output != NULL);
	hls_insert_scte35_info(output, 0, &period, &adaptation_set, &representation, &segment, GF_FALSE);
	fflush(output);
	rewind(output);
	memset(text, 0, sizeof(text));
	bytes_read = fread(text, 1, sizeof(text)-1, output);
	text[bytes_read] = 0;
	assert_true(strstr(text, "#EXT-X-CUE-IN") != NULL);
	assert_equal(event.state, 0, "%u");
	fclose(output);

	gf_list_del(event_stream.entries);
	gf_list_del(period.event_streams);
}

unittest(mpd_segment_event_associations_roundtrip)
{
	GF_DASH_SegmentContext *seg;
	GF_List *segments = gf_list_new();
	GF_MPD_Representation *rep = gf_mpd_representation_new();
	FILE *out = gf_file_temp(NULL);
	char serialized[8192];
	char xml[9000];
	GF_DOMParser *dom = gf_xml_dom_new();
	GF_MPD *mpd = gf_mpd_new();
	GF_List *restored;
	GF_DASH_SegmentContext *read_seg;
	GF_DASH_SegmentEventAssociation *a;
	GF_Err e;

	GF_SAFEALLOC(seg, GF_DASH_SegmentContext);
	assert_true(seg != NULL);
	seg->time = 1000;
	seg->dur = 600;
	seg->seg_num = 13;
	seg->filename = gf_strdup("segment13.m4s");
	assert_equal(gf_dash_segment_event_association_add(seg, "urn:scte:scte35:2014:xml+bin?x=1&y=2", 8076794552LL, 662, GF_DASH_SEGMENT_EVENT_START), GF_OK, "%d");
	assert_equal(gf_dash_segment_event_association_add(seg, "urn:scte:scte35:2014:xml+bin?x=1&y=2", -5LL, 662, GF_DASH_SEGMENT_EVENT_END), GF_OK, "%d");
	assert_equal(gf_list_count(seg->event_associations), 2, "%u");
	gf_list_add(segments, seg);
	gf_mpd_print_dasher_segments(out, segments, 0);
	gf_fseek(out, 0, SEEK_SET);
	size_t len = gf_fread(serialized, sizeof(serialized)-1, out);
	serialized[len] = 0;
	assert_true(strstr(serialized, "eventAssociation") != NULL);
	assert_true(strstr(serialized, "events_explicit=\"1\"") != NULL);
	assert_true(strstr(serialized, "boundary=\"2\"") != NULL);
	assert_true(strstr(serialized, "x=1&amp;y=2") != NULL);
	snprintf(xml, sizeof(xml), "<MPD xmlns:gpac=\"urn:gpac:filters:dasher:2018\">%s</MPD>", serialized);
	e = gf_xml_dom_parse_string(dom, xml);
	assert_equal(e, GF_OK, "%d");
	GF_XMLNode *segments_node = gf_list_get(gf_xml_dom_get_root(dom)->content, 0);
	restored = gf_mpd_parse_segments_context(mpd, segments_node);
	assert_true(restored != NULL);
	assert_equal(gf_list_count(restored), 1, "%u");
	read_seg = gf_list_get(restored, 0);
	assert_equal(gf_list_count(read_seg->event_associations), 2, "%u");
	a = gf_list_get(read_seg->event_associations, 0);
	assert_equal_str(a->scheme_id_uri, "urn:scte:scte35:2014:xml+bin?x=1&y=2");
	assert_equal(a->presentation_time, (s64)8076794552LL, LLD);
	assert_equal(a->id, 662, "%u");
	assert_equal(a->boundary_type, GF_DASH_SEGMENT_EVENT_START, "%u");
	a = gf_list_get(read_seg->event_associations, 1);
	assert_equal(a->boundary_type, GF_DASH_SEGMENT_EVENT_END, "%u");
	assert_equal(a->presentation_time, (s64)-5, LLD);

	/* Exercise the public owning representation's existing cleanup path. */
	rep->state_seg_list = segments;
	gf_mpd_representation_free(rep);
	rep = gf_mpd_representation_new();
	rep->state_seg_list = restored;
	gf_mpd_representation_free(rep);
	gf_xml_dom_del(dom);
	gf_mpd_del(mpd);
	gf_fclose(out);
}


/* The segment-context ABI maps SCTE events to physical boundaries. The writer
 * must not infer a different placement from each rendition's clock origin. */
unittest(mpd_hls_explicit_scte35_boundaries)
{
    GF_MPD *mpd = gf_mpd_new();
    GF_MPD_Period *period = NULL;
    GF_MPD_AdaptationSet *as = NULL;
    GF_MPD_Representation *rep = NULL;
    GF_MPD_EventStream *es;
    GF_MPD_EventStreamEntry *evt;
    GF_DASH_SegmentContext *sctx;
    char buf[16384];
    FILE *out = gf_file_temp(NULL);
    const char *scheme = "urn:scte:scte35:2014:xml+bin";
    const s64 source_pts = 8076794552LL;
    u32 i;
    size_t len;
    assert_true(mpd != NULL);
    assert_true(out != NULL);

    period = gf_mpd_period_new();
    assert_true(period != NULL);
    gf_list_add(mpd->periods, period);
    assert_true(period->event_streams != NULL);
    GF_SAFEALLOC(es, GF_MPD_EventStream);
    assert_true(es != NULL);
    es->scheme_id_uri = gf_strdup(scheme);
    es->timescale = 90000;
    es->entries = gf_list_new();
    GF_SAFEALLOC(evt, GF_MPD_EventStreamEntry);
    assert_true(evt != NULL);
    evt->id = 662;
    evt->presentation_time = source_pts;
    evt->duration = 18000000;
    gf_list_add(es->entries, evt);
    gf_list_add(period->event_streams, es);

    as = gf_mpd_adaptation_set_new();
    assert_true(as != NULL);
    rep = gf_mpd_representation_new();
    assert_true(rep != NULL);
    gf_list_add(as->representations, rep);
    gf_list_add(period->adaptation_sets, as);
    rep->state_seg_list = gf_list_new();
    rep->timescale = 90000;
    rep->timescale_mpd = 1000;
    rep->hls_max_seg_dur.num = 10;
    rep->hls_max_seg_dur.den = 1;
    as->starts_with_sap = GF_FILTER_SAP_1;
    for (i=0; i<3; i++) {
        const u32 numbers[3] = {12, 13, 53};
        const u64 times[3] = {120000, 130000, 530000};
        GF_SAFEALLOC(sctx, GF_DASH_SegmentContext);
        assert_true(sctx != NULL);
        sctx->time = times[i];
        sctx->dur = 900000; /* 10s at media timescale; time is 1k MPD */
        sctx->seg_num = numbers[i];
        {
            char name[32];
            snprintf(name, sizeof(name), "seg%u.m4s", numbers[i]);
            sctx->filename = gf_strdup(name);
        }
        /* An empty list explicitly disables the upstream clock-based inference. */
        sctx->event_associations = gf_list_new();
        assert_true(sctx->event_associations != NULL);
        if (numbers[i] == 13)
            assert_equal(gf_dash_segment_event_association_add(sctx, scheme, source_pts, 662, GF_DASH_SEGMENT_EVENT_START), GF_OK, "%d");
        else if (numbers[i] == 53)
            assert_equal(gf_dash_segment_event_association_add(sctx, scheme, source_pts, 662, GF_DASH_SEGMENT_EVENT_END), GF_OK, "%d");
        gf_list_add(rep->state_seg_list, sctx);
    }
    assert_true(hls_has_explicit_segment_events(rep));
    /* The old implementation would place CUE-OUT based on unrelated source
     * timestamps, and depend on mutable event state to generate CUE-IN. */
    assert_equal(gf_mpd_write_m3u8_playlist(mpd, period, as, rep, "test.m3u8", 7, 0, NULL, GF_FALSE, GF_TRUE, out), GF_OK, "%d");
    gf_fseek(out, 0, SEEK_SET);
    len = gf_fread(buf, sizeof(buf)-1, out);
    buf[len] = 0;
    assert_true(strstr(buf, "#EXT-X-CUE-OUT:200") != NULL);
    assert_true(strstr(buf, "#EXT-X-CUE-IN") != NULL);
    assert_true(strstr(buf, "#EXT-X-DATERANGE:ID=\"662-0000\"") != NULL);
    assert_equal(evt->state, 0, "%d");
    /* Exactly one OUT and one IN, at their annotated segment positions. */
    const char *cue_out = strstr(buf, "#EXT-X-CUE-OUT:");
    const char *cue_in = strstr(buf, "#EXT-X-CUE-IN");
    assert_true(cue_out && cue_in && cue_out < cue_in);
    assert_true(strstr(buf, "seg12.m4s") < cue_out);
    assert_true(cue_out < strstr(buf, "seg13.m4s"));
    assert_true(strstr(buf, "seg13.m4s") < cue_in);
    assert_true(cue_in < strstr(buf, "seg53.m4s"));
    assert_true(strstr(cue_out+1, "#EXT-X-CUE-OUT:") == NULL);
    assert_true(strstr(cue_in+1, "#EXT-X-CUE-IN") == NULL);
    /* Note: playlist has three artificial segments but their event positions
     * are independent of the source event's 90 kHz absolute clock. */
    gf_fclose(out);
    gf_mpd_del(mpd);
}

/* Empty explicit lists must persist: otherwise a restarted Dasher could
 * accidentally resume the legacy timestamp-based cue placement. */
unittest(mpd_hls_explicit_empty_segment_restart)
{
    GF_DASH_SegmentContext *sctx;
    GF_List *segments = gf_list_new();
    GF_List *restored;
    GF_MPD_Representation *rep = gf_mpd_representation_new();
    GF_DOMParser *dom = gf_xml_dom_new();
    GF_MPD *mpd = gf_mpd_new();
    FILE *out = gf_file_temp(NULL);
    char xml[4096], segment_xml[3700];
    size_t len;
    GF_Err e;

    GF_SAFEALLOC(sctx, GF_DASH_SegmentContext);
    assert_true(sctx != NULL);
    sctx->time = 15000;
    sctx->dur = 900000;
    sctx->filename = gf_strdup("segment15.m4s");
    sctx->seg_num = 15;
    sctx->event_associations = gf_list_new();
    assert_true(sctx->event_associations != NULL);
    gf_list_add(segments, sctx);
    gf_mpd_print_dasher_segments(out, segments, 0);
    gf_fseek(out, 0, SEEK_SET);
    len = gf_fread(segment_xml, sizeof(segment_xml)-1, out);
    segment_xml[len] = 0;
    assert_true(strstr(segment_xml, "events_explicit=\"1\"") != NULL);
    snprintf(xml, sizeof(xml), "<MPD xmlns:gpac=\"urn:gpac:filters:dasher:2018\">%s</MPD>", segment_xml);
    e = gf_xml_dom_parse_string(dom, xml);
    assert_equal(e, GF_OK, "%d");
    restored = gf_mpd_parse_segments_context(mpd, gf_list_get(gf_xml_dom_get_root(dom)->content, 0));
    assert_true(restored != NULL);
    assert_equal(gf_list_count(restored), 1, "%u");
    sctx = gf_list_get(restored, 0);
    assert_true(sctx->event_associations != NULL);
    assert_equal(gf_list_count(sctx->event_associations), 0, "%u");
    assert_equal(sctx->seg_num, 15, "%u");
    rep->state_seg_list = segments;
    gf_mpd_representation_free(rep);
    rep = gf_mpd_representation_new();
    rep->state_seg_list = restored;
    gf_mpd_representation_free(rep);
    gf_xml_dom_del(dom);
    gf_mpd_del(mpd);
    gf_fclose(out);
}


/*************************************/

unittest(mpd_hls_scte35_metadata_and_finite_return_policy)
{
	GF_MPD_Period period = {0};
	GF_MPD_AdaptationSet as = {0};
	GF_MPD_Representation rep = {0};
	GF_MPD_EventStream es = {0};
	GF_MPD_EventStreamEntry event = {0};
	GF_DASH_SegmentContext segments[3] = {{0}};
	FILE *out;
	char text[4096] = {0};
	size_t n;

	period.event_streams = gf_list_new();
	es.entries = gf_list_new();
	rep.state_seg_list = gf_list_new();
	assert_true(period.event_streams && es.entries && rep.state_seg_list);
	rep.timescale = es.timescale = 1000;
	event.id = 100;
	event.presentation_time = 1920;
	event.duration = 1000;
	gf_list_add(es.entries, &event);
	gf_list_add(period.event_streams, &es);
	segments[0].time = 0; segments[0].dur = 6000; segments[0].filename = "seg1.m4s";
	segments[1].time = 6000; segments[1].dur = 960; segments[1].filename = "seg2.m4s";
	gf_list_add(rep.state_seg_list, &segments[0]);
	gf_list_add(rep.state_seg_list, &segments[1]);

	/* Short event entirely within one long segment: keep SCTE metadata but
	 * never fabricate an ad opportunity or leave a dangling OUT at ENDLIST. */
	out = tmpfile(); assert_true(out != NULL);
	hls_insert_scte35_info(out, 0, &period, &as, &rep, &segments[0], GF_TRUE);
	fflush(out); rewind(out);
	n = fread(text, 1, sizeof(text)-1, out); text[n] = 0;
	assert_true(strstr(text, "#EXT-X-DATERANGE:ID=\"100-0000\"") != NULL);
	assert_true(strstr(text, "#EXT-X-CUE-OUT") == NULL);
	assert_true(strstr(text, "#EXT-X-CUE-IN") == NULL);
	assert_equal(event.state, 0, "%u");
	fclose(out);

	/* A short event with an actual start AND return segment boundary is a
	 * valid replacement opportunity on a finalized playlist. */
	segments[0].dur = 2000;
	segments[1].time = 2000; segments[1].dur = 1000;
	segments[2].time = 3000; segments[2].dur = 1000; segments[2].filename = "seg3.m4s";
	gf_list_add(rep.state_seg_list, &segments[2]);
	out = tmpfile(); assert_true(out != NULL);
	hls_insert_scte35_info(out, 0, &period, &as, &rep, &segments[1], GF_TRUE);
	assert_true(hls_scte35_has_return_boundary(&rep, 2920));
	fflush(out); rewind(out); memset(text, 0, sizeof(text));
	n = fread(text, 1, sizeof(text)-1, out); text[n] = 0;
	assert_true(strstr(text, "#EXT-X-CUE-OUT:1") != NULL);
	assert_equal(event.state, 1, "%u");
	fclose(out);
	out = tmpfile(); assert_true(out != NULL);
	hls_insert_scte35_info(out, 0, &period, &as, &rep, &segments[2], GF_TRUE);
	fflush(out); rewind(out); memset(text, 0, sizeof(text));
	n = fread(text, 1, sizeof(text)-1, out); text[n] = 0;
	assert_true(strstr(text, "#EXT-X-CUE-IN") != NULL);
	fclose(out);

	/* A rolling live playlist beginning mid-break retains DATERANGE. */
	rep.tsb_first_entry = 1;
	event.state = 0;
	out = tmpfile(); assert_true(out != NULL);
	hls_insert_scte35_info(out, 0, &period, &as, &rep, &segments[1], GF_FALSE);
	fflush(out); rewind(out); memset(text, 0, sizeof(text));
	n = fread(text, 1, sizeof(text)-1, out); text[n] = 0;
	assert_true(strstr(text, "#EXT-X-DATERANGE:ID=\"100-0000\"") != NULL);
	assert_true(strstr(text, "#EXT-X-CUE-OUT:1") != NULL);
	fclose(out);

	gf_list_del(rep.state_seg_list);
	gf_list_del(es.entries);
	gf_list_del(period.event_streams);
}
