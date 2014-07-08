#include "global.h"
#include "correct_aln_stats.h"
#include "util.h"

void correct_aln_stats_reset(CorrectAlnStats *stats) {
  memset(stats, 0, sizeof(CorrectAlnStats));
}

void correct_aln_stats_merge(CorrectAlnStats *restrict dst,
                             CorrectAlnStats *restrict src)
{
  size_t i, j;
  for(i = 0; i < ALN_STATS_MAX_GAP; i++)
    for(j = 0; j < ALN_STATS_MAX_GAP; j++)
      dst->gap_err_histgrm[i][j] += src->gap_err_histgrm[i][j];

  for(i = 0; i < ALN_STATS_MAX_FRAGLEN; i++)
    dst->fraglen_histgrm[i] += src->fraglen_histgrm[i];

  dst->num_gap_attempts += src->num_gap_attempts;
  dst->num_gap_successes += src->num_gap_successes;
  dst->num_paths_disagreed += src->num_paths_disagreed;
  dst->num_gaps_too_short += src->num_gaps_too_short;
}

// Sequencing error gap
void correct_aln_stats_add(CorrectAlnStats *stats,
                           size_t exp_seq_gap, size_t act_gap)
{
  exp_seq_gap = MIN2(exp_seq_gap, ALN_STATS_MAX_GAP-1);
  act_gap     = MIN2(act_gap,     ALN_STATS_MAX_GAP-1);
  stats->gap_err_histgrm[exp_seq_gap][act_gap]++;
}

// @exp_seq_gap does not include mate pair gap
void correct_aln_stats_add_mp(CorrectAlnStats *stats,
                              size_t exp_seq_gap, size_t gap_kmers,
                              size_t r1bases, size_t r2bases,
                              size_t kmer_size)
{
  // We want to record fragment length in kmers, therefore:
  //   frag_kmers = (r1-k+1) + gap_kmers + (r2-k+1)
  //   frag_bp = frag_kmers+k-1 = r1+r2 + gap_kmers - k + 1
  size_t fraglen_bp = r1bases + r2bases + gap_kmers -
                      (long)(exp_seq_gap + kmer_size - 1);
  fraglen_bp = MIN2(fraglen_bp, ALN_STATS_MAX_FRAGLEN-1);
  stats->fraglen_histgrm[fraglen_bp]++;
}

// Save gap size distribution matrix
void correct_aln_stats_dump_gaps(const CorrectAlnStats *stats, const char *path)
{
  status("[CorrectAln] Saving gap size distribution to: %s", path);

  size_t i, j;
  FILE *fout = fopen(path, "w");
  if(fout == NULL) { warn("Cannot cannot open: %s", path); return; }

  fputc('.', fout); // top left cell is just .
  for(j = 0; j < ALN_STATS_MAX_GAP; j++)
    fprintf(fout, "\tgraph_%zu", j);
  fputc('\n', fout);

  for(i = 0; i < ALN_STATS_MAX_GAP; i++) {
    fprintf(fout, "read_%zu", i);
    for(j = 0; j < ALN_STATS_MAX_GAP; j++) {
      fprintf(fout, "\t%zu", stats->gap_err_histgrm[i][j]);
    }
    fputc('\n', fout);
  }
  fclose(fout);
}

// Save fragment size vector
void correct_aln_stats_dump_fraglen(const CorrectAlnStats *stats, const char *path)
{
  status("[CorrectAln] Saving fragment length distribution to: %s", path);

  size_t i;
  FILE *fout = fopen(path, "w");
  if(fout == NULL) { warn("Cannot cannot open: %s", path); return; }

  fprintf(fout, "fraglen_bases\tcount\n");

  for(i = 0; i < ALN_STATS_MAX_FRAGLEN; i++) {
    fprintf(fout, "%4zu\t%4zu\n", i, stats->fraglen_histgrm[i]);
  }
  fclose(fout);
}

void correct_aln_stats_print_summary(const CorrectAlnStats *stats,
                                     size_t num_reads, size_t num_read_pairs)
{
  if(stats->num_gap_attempts == 0) {
    status("[CorrectAln] No gap attempts");
    return;
  }

  size_t i, j;
  size_t exp_gaps[ALN_STATS_MAX_GAP] = {0}, act_gaps[ALN_STATS_MAX_GAP] = {0};
  size_t num_seq_gaps = 0, num_frags_resolved = 0;
  size_t sum_gap_diff = 0, sum_exp_gap = 0, sum_act_gap = 0, sum_fraglen = 0;
  size_t exp_gap_mode = 0, exp_gap_maxc = 0; // maxc is max count
  size_t act_gap_mode = 0, act_gap_maxc = 0;
  size_t fraglen_mode = 0, fraglen_maxc = 0;

  for(i = 0; i < ALN_STATS_MAX_GAP; i++) {
    for(j = 0; j < ALN_STATS_MAX_GAP; j++) {
      exp_gaps[i]  += stats->gap_err_histgrm[i][j];
      act_gaps[j]  += stats->gap_err_histgrm[i][j];
      num_seq_gaps += stats->gap_err_histgrm[i][j];
      sum_gap_diff += labs((long)i-j)*stats->gap_err_histgrm[i][j];
    }
  }

  // Mean, sum
  for(i = 0; i < ALN_STATS_MAX_GAP; i++) {
    sum_exp_gap += i * exp_gaps[i];
    sum_act_gap += i * act_gaps[i];
    if(exp_gaps[i] > exp_gap_maxc) { exp_gap_mode = i; exp_gap_maxc = exp_gaps[i]; }
    if(act_gaps[i] > act_gap_maxc) { act_gap_mode = i; act_gap_maxc = act_gaps[i]; }
  }

  for(i = 0; i < ALN_STATS_MAX_FRAGLEN; i++) {
    size_t curr = stats->fraglen_histgrm[i];
    num_frags_resolved += curr;
    sum_fraglen        += i * curr;
    if(curr > fraglen_maxc) { fraglen_mode = i; fraglen_maxc = curr; }
  }

  if(num_seq_gaps == 0)
  {
    status("[CorrectAln] Couldn't traverse any sequence gaps");
  }
  else
  {
    ctx_assert(num_reads > 0);

    // Medians
    float exp_gap_median = find_hist_median(exp_gaps, ALN_STATS_MAX_GAP);
    float act_gap_median = find_hist_median(act_gaps, ALN_STATS_MAX_GAP);

    // Means
    float exp_gap_mean = (double)sum_exp_gap / num_seq_gaps;
    float act_gap_mean = (double)sum_act_gap / num_seq_gaps;

    float gap_diff_mean = (double)sum_gap_diff / num_seq_gaps;

    float gaps_per_read_mean = (double)num_seq_gaps / num_reads;

    // Print seqn gap statistics
    status("[CorrectAln] SE: expected gap mean: %.1f median: %.1f mode: %zu (%zu)",
           exp_gap_mean, exp_gap_median, exp_gap_mode, exp_gap_maxc);
    status("[CorrectAln] SE: actual   gap mean: %.1f median: %.1f mode: %zu (%zu)",
           act_gap_mean, act_gap_median, act_gap_mode, act_gap_maxc);
    status("[CorrectAln]     %.3f gaps per read; mean gap length diff: %.2f",
           gaps_per_read_mean, gap_diff_mean);
  }

  // Print fragment size statistics
  ctx_assert2(num_frags_resolved <= num_read_pairs, "%zu, %zu",
              num_frags_resolved,   num_read_pairs);

  if(num_read_pairs == 0)
  {
    status("[CorrectAln] Didn't see any PE reads");
  }
  else
  {
    float fragments_recovered_pct = (100.0 * num_frags_resolved) / num_read_pairs;

    // Print stats
    char num_read_pairs_str[50], num_frags_resolved_str[50];
    ulong_to_str(num_frags_resolved, num_frags_resolved_str);
    ulong_to_str(num_read_pairs, num_read_pairs_str);

    status("[CorrectAln] PE: traversed %s / %s (%.2f%%) insert gaps",
           num_frags_resolved_str, num_read_pairs_str, fragments_recovered_pct);

    if(num_frags_resolved > 0)
    {
      char fraglen_mean_str[50], fraglen_median_str[50];
      char fraglen_mode_str[50], fraglen_modeval_str[50];
      size_t fraglen_mean, fraglen_median;

      float frag_meanf, frag_medianf;
      frag_medianf = find_hist_median(stats->fraglen_histgrm, ALN_STATS_MAX_FRAGLEN);
      frag_meanf = (double)sum_fraglen / num_frags_resolved;

      // If all bins 0, find_hist_median returns -1
      ctx_assert(frag_medianf >= 0);

      // Round and convert to unsigned
      fraglen_median = frag_medianf+0.5;
      fraglen_mean = frag_meanf+0.5;

      ulong_to_str(fraglen_mean, fraglen_mean_str);
      ulong_to_str(fraglen_median, fraglen_median_str);
      ulong_to_str(fraglen_mode, fraglen_mode_str);
      ulong_to_str(fraglen_maxc, fraglen_modeval_str);
      status("[CorrectAln] fragment length (bp): mean: %s median: %s mode: %s (%s)",
             fraglen_mean_str, fraglen_median_str,
             fraglen_mode_str, fraglen_modeval_str);
    }
  }

  // General Stats
  char num_gap_attempts_str[100], num_gap_successes_str[100];
  char num_paths_disagree_str[100], num_gaps_too_short_str[100];
  ulong_to_str(stats->num_gap_attempts, num_gap_attempts_str);
  ulong_to_str(stats->num_gap_successes, num_gap_successes_str);
  ulong_to_str(stats->num_paths_disagreed, num_paths_disagree_str);
  ulong_to_str(stats->num_gaps_too_short, num_gaps_too_short_str);

  status("[CorrectAln] traversals succeeded: %s / %s (%.2f%%)",
         num_gap_successes_str, num_gap_attempts_str,
         (100.0 * stats->num_gap_successes) / stats->num_gap_attempts);
  status("[CorrectAln] failed path check: %s / %s (%.2f%%)",
         num_paths_disagree_str, num_gap_attempts_str,
         (100.0 * stats->num_paths_disagreed) / stats->num_gap_attempts);
  status("[CorrectAln] too short: %s / %s (%.2f%%)",
         num_gaps_too_short_str, num_gap_attempts_str,
         (100.0 * stats->num_gaps_too_short) / stats->num_gap_attempts);
}

// Print summary stats and write output files
// @ht_num_kmers is the number of kmers loaded into the graph
void correct_aln_dump_stats(const LoadingStats *stats,
                            const CorrectAlnStats *gapstats,
                            const char *dump_seqgap_hist_path,
                            const char *dump_fraglen_hist_path,
                            size_t ht_num_kmers)
{
  correct_aln_stats_print_summary(gapstats, stats->num_se_reads,
                                  stats->num_pe_reads/2);

  // Print mp gap size / insert stats to a file
  if(dump_seqgap_hist_path != NULL) {
    correct_aln_stats_dump_gaps(gapstats, dump_seqgap_hist_path);
  }

  if(stats->num_pe_reads > 0 && dump_fraglen_hist_path != NULL) {
    correct_aln_stats_dump_fraglen(gapstats, dump_fraglen_hist_path);
  }

  loading_stats_print_summary(stats, ht_num_kmers);
}