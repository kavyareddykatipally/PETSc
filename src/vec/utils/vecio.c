#ifndef lint
static char vcid[] = "$Id: vecio.c,v 1.4 1995/08/23 17:07:56 curfman Exp bsmith $";
#endif

/* 
   This file contains simple binary read/write routines for vectors.
 */

#include "petsc.h"
#include "vec/vecimpl.h"
#include "sysio.h"
#include "pinclude/pviewer.h"

/*@ 
  VecLoad - Loads a vector that has been stored in binary format
  with VecView().

  Input Parameters:
. comm - MPI communicator
. viewer - binary file viewer, obtained from ViewerFileOpenBinary()
. outtype - type of output vector
. ind - optional index set of local vector indices (or 0 for loading
  the entire vector on each processor)

  Output Parameter:
. newvec - the newly loaded vector

  Notes:
  Currently, the input file must contain the full global vector, as
  written by the routine VecView().  Only those vector indices that
  are specified by the index set "ind" are read into the local vector
  segment on a given processor. 
@*/  
int VecLoad(MPI_Comm comm,Viewer bview,VecType outtype,IS ind,Vec *newvec)
{
  int    i, rows, ierr, lsize, gsize, *pind, low, high, iglobal, type, fd;
  Vec    vec, tempvec;
  Scalar *avec;
  PetscObject vobj = (PetscObject) bview;

  PETSCVALIDHEADERSPECIFIC(vobj,VIEWER_COOKIE);
  if (vobj->type != BIN_FILES_VIEWER && vobj->type != BIN_FILES_VIEWER)
   SETERRQ(1,"VecLoad: Invalid viewer; open viewer with ViewerFileOpenBinary().");
  fd = ViewerFileGetDescriptor_Private(bview);

  /* Read vector header.  Should this really be the full header? */
  ierr = SYRead(fd,(char *)&type,sizeof(int),SYINT); CHKERRQ(ierr);
  if ((VecType)type != VECSEQ)
    SETERRQ(1,"VecLoadBinary: Only VECSEQ input format supported.");
  ierr = SYRead(fd,(char *)&rows,sizeof(int),SYINT); CHKERRQ(ierr);

  /* Read vector contents */
  if (!ind) {
    if (outtype == VECSEQ) {
      ierr = VecCreateSequential(MPI_COMM_SELF,rows,&vec); CHKERRQ(ierr);
      ierr = VecGetArray(vec,&avec); CHKERRQ(ierr);
      ierr = SYRead(fd,(char *)avec,rows*sizeof(Scalar),SYSCALAR); CHKERRQ(ierr);
    } else SETERRQ(1,"Must specify index set for parallel input.");
  }
  else {
    /* We should change this to allow reading partial vector */
    ierr = ISGetLocalSize(ind,&lsize); CHKERRQ(ierr);
    MPI_Allreduce(&lsize,&gsize,1,MPI_INT,MPI_SUM,comm);
    if (gsize != rows) SETERRQ(1,"Incompatible parallel vector length.");
    if (outtype == VECSEQ) {
      ierr = VecCreateSequential(MPI_COMM_SELF,rows,&vec); CHKERRQ(ierr);
      ierr = VecGetArray(vec,&avec); CHKERRQ(ierr);
      ierr = SYRead(fd,(char *)avec,rows*sizeof(Scalar),SYSCALAR); CHKERRQ(ierr);
    } else if (outtype == VECMPI) {
      ierr = VecCreateMPI(comm,lsize,rows,&vec); CHKERRQ(ierr);
      ierr = VecGetOwnershipRange(vec,&low,&high);
      ierr = VecCreateSequential(MPI_COMM_SELF,rows,&tempvec); CHKERRQ(ierr);
      ierr = VecGetArray(tempvec,&avec); CHKERRQ(ierr);
      ierr = SYRead(fd,(char *)avec,rows*sizeof(Scalar),SYSCALAR); CHKERRQ(ierr);
      ierr = ISGetIndices(ind,&pind); CHKERRQ(ierr);
      for (i=0; i<lsize; i++) {
        iglobal = i + low;
        ierr = VecSetValues(vec,1,&iglobal,&avec[pind[i]],INSERTVALUES);
        CHKERRQ(ierr);
      }
      ierr = VecDestroy(tempvec); CHKERRQ(ierr);
      ierr = ISRestoreIndices(ind,&pind); CHKERRQ(ierr);
    } else {
     SETERRQ(1,"Only VECSEQ and VECMPI output vectors are supported.");
    }
  }
  ierr = VecAssemblyBegin(vec); CHKERRQ(ierr);
  ierr = VecAssemblyEnd(vec); CHKERRQ(ierr);
  *newvec = vec;
  return 0;
}

/*@ 
  VecViewBinary - Writes a vector in binary format to the specified file.
  VecLoadBinary() can be used to load the resulting file.

  Input Parameters:
. newvec - the vector
. fd - file descriptor

  Notes:
  Currently, this routine writes the complete global vector to a single file.
@*/  
int VecViewBinary(Vec v,int fd)
{
  int    length, ierr;
  Scalar *va;

  PETSCVALIDHEADERSPECIFIC(v,VEC_COOKIE);
  ierr = VecGetSize(v,&length); CHKERRQ(ierr);
  if (v->type == VECMPI) {
    int     i, rstart, rend, mytid, numtids, *iglobal, ldim;
    Vec     v2;
    VecType ntype;
    MPI_Comm_rank(v->comm,&mytid); 
    MPI_Comm_size(v->comm,&numtids);
    if (!mytid)
      ierr = VecCreateMPI(MPI_COMM_WORLD,length,length,&v2);
    else
      ierr = VecCreateMPI(MPI_COMM_WORLD,0,length,&v2);
    CHKERRQ(ierr);
    ierr = VecGetOwnershipRange(v,&rstart,&rend); CHKERRQ(ierr); 
    ierr = VecGetArray(v,&va); CHKERRQ(ierr);
    ierr = VecGetLocalSize(v,&ldim); CHKERRQ(ierr);
    iglobal = (int *) PETSCMALLOC(ldim*sizeof(int));
    for (i=0; i<ldim; i++) iglobal[i] = i + rstart;
    ierr = VecSetValues(v2,ldim,iglobal,va,INSERTVALUES); CHKERRQ(ierr);
    PETSCFREE(iglobal);
    ierr = VecRestoreArray(v,&va); CHKERRQ(ierr);
    ierr = VecAssemblyBegin(v2); CHKERRQ(ierr);

    /* Write vector header */
    ntype = VECSEQ;
    ierr = SYWrite(fd,(char *)&ntype,sizeof(int),SYINT,0); CHKERRQ(ierr);
    ierr = SYWrite(fd,(char *)&length,sizeof(int),SYINT,0); CHKERRQ(ierr);

    /* Write vector contents */
    ierr = VecAssemblyEnd(v2); CHKERRQ(ierr);
    ierr = VecGetArray(v2,&va); CHKERRQ(ierr);
    ierr = SYWrite(fd,(char *)va,length*sizeof(Scalar),SYSCALAR,0); CHKERRQ(ierr);
    ierr = VecRestoreArray(v,&va); CHKERRQ(ierr);
    ierr = VecDestroy(v2); CHKERRQ(ierr);
  }
  else if (v->type == VECSEQ) {
    /* Write vector header */
    ierr = SYWrite(fd,(char *)&v->type,sizeof(int),SYINT,0); CHKERRQ(ierr);
    ierr = VecGetSize(v,&length); CHKERRQ(ierr);
    ierr = SYWrite(fd,(char *)&length,sizeof(int),SYINT,0); CHKERRQ(ierr);

    /* Write vector contents */
    ierr = VecGetArray(v,&va); CHKERRQ(ierr);
    ierr = SYWrite(fd,(char *)va,length*sizeof(Scalar),SYINT,0); CHKERRQ(ierr);
    ierr = VecRestoreArray(v,&va); CHKERRQ(ierr);
  }
  else SETERRQ(1,"VecViewBinary: Only VECSEQ and VECMPI are currently supported");

  return 0;
}
