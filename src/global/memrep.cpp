/*!
  @file data_ops.H
  @brief Agglomeration of useful data operations
  @author Robert Marskar
  @date Nov 2017
*/

#include "memrep.H"

#include <EBLevelDataOps.H>
#include <memtrack.H>
#include <memusage.H>

void memrep::get_max_min_memory(){
  int BytesPerMB = 1024*1024;
  long long curMem;
  long long peakMem;
  overallMemoryUsage(curMem, peakMem);

  int unfreed_mem = curMem;
  int peak_mem    = peakMem;

  int max_unfreed_mem;
  int max_peak_mem;
  int min_unfreed_mem;
  int min_peak_mem;

  int result1 = MPI_Allreduce(&unfreed_mem, &max_unfreed_mem, 1, MPI_INT, MPI_MAX, Chombo_MPI::comm);
  int result2 = MPI_Allreduce(&peak_mem,    &max_peak_mem,    1, MPI_INT, MPI_MAX, Chombo_MPI::comm);
  int result3 = MPI_Allreduce(&unfreed_mem, &min_unfreed_mem, 1, MPI_INT, MPI_MIN, Chombo_MPI::comm);
  int result4 = MPI_Allreduce(&peak_mem,    &min_peak_mem,    1, MPI_INT, MPI_MIN, Chombo_MPI::comm);

  pout() << "memrep::get_max_min_memory:" 
	 << "\t max peak = "       << 1.0*max_peak_mem/BytesPerMB 
         << "\t min peak = "    << 1.0*min_peak_mem/BytesPerMB 
         << "\t max unfreed = " << 1.0*max_unfreed_mem/BytesPerMB
         << "\t min unfreed = " << 1.0*min_unfreed_mem/BytesPerMB << endl;
}
