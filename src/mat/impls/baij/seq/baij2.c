#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: baij2.c,v 1.22 1998/01/28 21:02:12 bsmith Exp bsmith $";
#endif

#include "pinclude/pviewer.h"
#include "sys.h"
#include "src/mat/impls/baij/seq/baij.h"
#include "src/vec/vecimpl.h"
#include "src/inline/spops.h"
#include "petsc.h"
#include "src/inline/bitarray.h"


#undef __FUNC__  
#define __FUNC__ "MatIncreaseOverlap_SeqBAIJ"
int MatIncreaseOverlap_SeqBAIJ(Mat A,int is_max,IS *is,int ov)
{
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ *) A->data;
  int         row, i,j,k,l,m,n, *idx,ierr, *nidx, isz, val, ival;
  int         start, end, *ai, *aj,bs,*nidx2;
  BT          table;

  PetscFunctionBegin;
  m     = a->mbs;
  ai    = a->i;
  aj    = a->j;
  bs    = a->bs;

  if (ov < 0)  SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"Negative overlap specified");

  ierr  = BTCreate(m,table); CHKERRQ(ierr);
  nidx  = (int *) PetscMalloc((m+1)*sizeof(int)); CHKPTRQ(nidx); 
  nidx2 = (int *)PetscMalloc((a->m+1)*sizeof(int)); CHKPTRQ(nidx2);

  for ( i=0; i<is_max; i++ ) {
    /* Initialise the two local arrays */
    isz  = 0;
    BTMemzero(m,table);
                 
    /* Extract the indices, assume there can be duplicate entries */
    ierr = ISGetIndices(is[i],&idx);  CHKERRQ(ierr);
    ierr = ISGetSize(is[i],&n);  CHKERRQ(ierr);

    /* Enter these into the temp arrays i.e mark table[row], enter row into new index */
    for ( j=0; j<n ; ++j){
      ival = idx[j]/bs; /* convert the indices into block indices */
      if (ival>m) SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"index greater than mat-dim");
      if(!BTLookupSet(table, ival)) { nidx[isz++] = ival;}
    }
    ierr = ISRestoreIndices(is[i],&idx);  CHKERRQ(ierr);
    ierr = ISDestroy(is[i]); CHKERRQ(ierr);
    
    k = 0;
    for ( j=0; j<ov; j++){ /* for each overlap*/
      n = isz;
      for ( ; k<n ; k++){ /* do only those rows in nidx[k], which are not done yet */
        row   = nidx[k];
        start = ai[row];
        end   = ai[row+1];
        for ( l = start; l<end ; l++){
          val = aj[l];
          if (!BTLookupSet(table,val)) {nidx[isz++] = val;}
        }
      }
    }
    /* expand the Index Set */
    for (j=0; j<isz; j++ ) {
      for (k=0; k<bs; k++ )
        nidx2[j*bs+k] = nidx[j]*bs+k;
    }
    ierr = ISCreateGeneral(PETSC_COMM_SELF, isz*bs, nidx2, (is+i)); CHKERRQ(ierr);
  }
  BTDestroy(table);
  PetscFree(nidx);
  PetscFree(nidx2);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatGetSubMatrix_SeqBAIJ_Private"
int MatGetSubMatrix_SeqBAIJ_Private(Mat A,IS isrow,IS iscol,int cs,MatGetSubMatrixCall scall,Mat *B)
{
  Mat_SeqBAIJ  *a = (Mat_SeqBAIJ *) A->data,*c;
  int          nznew, *smap, i, k, kstart, kend, ierr, oldcols = a->nbs,*lens;
  int          row,mat_i,*mat_j,tcol,*mat_ilen;
  int          *irow, *icol, nrows, ncols,*ssmap,bs=a->bs, bs2=a->bs2;
  int          *aj = a->j, *ai = a->i;
  Scalar       *mat_a;
  Mat          C;

  PetscFunctionBegin;
  ierr = ISSorted(iscol,(PetscTruth*)&i);
  if (!i) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"IS is not sorted");

  ierr = ISGetIndices(isrow,&irow); CHKERRQ(ierr);
  ierr = ISGetIndices(iscol,&icol); CHKERRQ(ierr);
  ierr = ISGetSize(isrow,&nrows); CHKERRQ(ierr);
  ierr = ISGetSize(iscol,&ncols); CHKERRQ(ierr);

  smap  = (int *) PetscMalloc((1+oldcols)*sizeof(int)); CHKPTRQ(smap);
  ssmap = smap;
  lens  = (int *) PetscMalloc((1+nrows)*sizeof(int)); CHKPTRQ(lens);
  PetscMemzero(smap,oldcols*sizeof(int));
  for ( i=0; i<ncols; i++ ) smap[icol[i]] = i+1;
  /* determine lens of each row */
  for (i=0; i<nrows; i++) {
    kstart  = ai[irow[i]]; 
    kend    = kstart + a->ilen[irow[i]];
    lens[i] = 0;
      for ( k=kstart; k<kend; k++ ) {
        if (ssmap[aj[k]]) {
          lens[i]++;
        }
      }
    }
  /* Create and fill new matrix */
  if (scall == MAT_REUSE_MATRIX) {
    c = (Mat_SeqBAIJ *)((*B)->data);

    if (c->mbs!=nrows || c->nbs!=ncols || c->bs!=bs) SETERRQ(PETSC_ERR_ARG_SIZ,0,"Submatrix wrong size");
    if (PetscMemcmp(c->ilen,lens, c->mbs *sizeof(int))) {
      SETERRQ(PETSC_ERR_ARG_SIZ,0,"Cannot reuse matrix. wrong no of nonzeros");
    }
    PetscMemzero(c->ilen,c->mbs*sizeof(int));
    C = *B;
  } else {  
    ierr = MatCreateSeqBAIJ(A->comm,bs,nrows*bs,ncols*bs,0,lens,&C);CHKERRQ(ierr);
  }
  c = (Mat_SeqBAIJ *)(C->data);
  for (i=0; i<nrows; i++) {
    row    = irow[i];
    nznew  = 0;
    kstart = ai[row]; 
    kend   = kstart + a->ilen[row];
    mat_i  = c->i[i];
    mat_j  = c->j + mat_i; 
    mat_a  = c->a + mat_i*bs2;
    mat_ilen = c->ilen + i;
    for ( k=kstart; k<kend; k++ ) {
      if ((tcol=ssmap[a->j[k]])) {
        *mat_j++ = tcol - 1;
        PetscMemcpy(mat_a,a->a+k*bs2,bs2*sizeof(Scalar)); mat_a+=bs2;
        (*mat_ilen)++;
      }
    }
  }
    
  /* Free work space */
  ierr = ISRestoreIndices(iscol,&icol); CHKERRQ(ierr);
  PetscFree(smap); PetscFree(lens);
  ierr = MatAssemblyBegin(C,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(C,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  
  ierr = ISRestoreIndices(isrow,&irow); CHKERRQ(ierr);
  *B = C;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatGetSubMatrix_SeqBAIJ"
int MatGetSubMatrix_SeqBAIJ(Mat A,IS isrow,IS iscol,int cs,MatGetSubMatrixCall scall,Mat *B)
{
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ *) A->data;
  IS          is1,is2;
  int         *vary,*iary,*irow,*icol,nrows,ncols,i,ierr,bs=a->bs,count;

  PetscFunctionBegin;
  ierr = ISGetIndices(isrow,&irow); CHKERRQ(ierr);
  ierr = ISGetIndices(iscol,&icol); CHKERRQ(ierr);
  ierr = ISGetSize(isrow,&nrows); CHKERRQ(ierr);
  ierr = ISGetSize(iscol,&ncols); CHKERRQ(ierr);
  
  /* Verify if the indices corespond to each element in a block 
   and form the IS with compressed IS */
  vary = (int *) PetscMalloc(2*(a->mbs+1)*sizeof(int)); CHKPTRQ(vary);
  iary = vary + a->mbs;
  PetscMemzero(vary,(a->mbs)*sizeof(int));
  for ( i=0; i<nrows; i++) vary[irow[i]/bs]++;
  count = 0;
  for (i=0; i<a->mbs; i++) {
    if (vary[i]!=0 && vary[i]!=bs) SETERRA(1,0,"Index set does not match blocks");
    if (vary[i]==bs) iary[count++] = i;
  }
  ierr = ISCreateGeneral(PETSC_COMM_SELF, count, iary,&is1); CHKERRQ(ierr);
  
  PetscMemzero(vary,(a->mbs)*sizeof(int));
  for ( i=0; i<ncols; i++) vary[icol[i]/bs]++;
  count = 0;
  for (i=0; i<a->mbs; i++) {
    if (vary[i]!=0 && vary[i]!=bs) SETERRA(1,0,"MatGetSubmatrices_SeqBAIJ:");
    if (vary[i]==bs) iary[count++] = i;
  }
  ierr = ISCreateGeneral(PETSC_COMM_SELF, count, iary,&is2); CHKERRQ(ierr);
  ierr = ISRestoreIndices(isrow,&irow); CHKERRQ(ierr);
  ierr = ISRestoreIndices(iscol,&icol); CHKERRQ(ierr);
  PetscFree(vary);

  ierr = MatGetSubMatrix_SeqBAIJ_Private(A,is1,is2,cs,scall,B); CHKERRQ(ierr);
  ISDestroy(is1);
  ISDestroy(is2);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatGetSubMatrices_SeqBAIJ"
int MatGetSubMatrices_SeqBAIJ(Mat A,int n, IS *irow,IS *icol,MatGetSubMatrixCall scall,Mat **B)
{
  int ierr,i;

  PetscFunctionBegin;
  if (scall == MAT_INITIAL_MATRIX) {
    *B = (Mat *) PetscMalloc( (n+1)*sizeof(Mat) ); CHKPTRQ(*B);
  }

  for ( i=0; i<n; i++ ) {
    ierr = MatGetSubMatrix_SeqBAIJ(A,irow[i],icol[i],PETSC_DECIDE,scall,&(*B)[i]);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}


/* -------------------------------------------------------*/
/* Should check that shapes of vectors and matrices match */
/* -------------------------------------------------------*/
#include "pinclude/plapack.h"

#undef __FUNC__  
#define __FUNC__ "MatMult_SeqBAIJ_1"
int MatMult_SeqBAIJ_1(Mat A,Vec xx,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*z,*v,sum;
  int             mbs=a->mbs,i,*idx,*ii,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n    = ii[1] - ii[0]; ii++;
    sum  = 0.0;
    while (n--) sum += *v++ * x[*idx++];
    z[i] = sum;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(2*a->nz - a->m);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMult_SeqBAIJ_2"
int MatMult_SeqBAIJ_2(Mat A,Vec xx,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*z,*v,*xb,sum1,sum2;
  register Scalar x1,x2;
  int             mbs=a->mbs,i,*idx,*ii,j,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = 0.0; sum2 = 0.0;
    for ( j=0; j<n; j++ ) {
      xb = x + 2*(*idx++); x1 = xb[0]; x2 = xb[1];
      sum1 += v[0]*x1 + v[2]*x2;
      sum2 += v[1]*x1 + v[3]*x2;
      v += 4;
    }
    z[0] = sum1; z[1] = sum2;
    z += 2;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(4*a->nz - a->m);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMult_SeqBAIJ_3"
int MatMult_SeqBAIJ_3(Mat A,Vec xx,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*z,*v,*xb,sum1,sum2,sum3,x1,x2,x3;
  int             mbs=a->mbs,i,*idx,*ii,j,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = 0.0; sum2 = 0.0; sum3 = 0.0;
    for ( j=0; j<n; j++ ) {
      xb = x + 3*(*idx++); x1 = xb[0]; x2 = xb[1]; x3 = xb[2];
      sum1 += v[0]*x1 + v[3]*x2 + v[6]*x3;
      sum2 += v[1]*x1 + v[4]*x2 + v[7]*x3;
      sum3 += v[2]*x1 + v[5]*x2 + v[8]*x3;
      v += 9;
    }
    z[0] = sum1; z[1] = sum2; z[2] = sum3;
    z += 3;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(18*a->nz - a->m);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMult_SeqBAIJ_4"
int MatMult_SeqBAIJ_4(Mat A,Vec xx,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*z,*v,*xb,sum1,sum2,sum3,sum4,x1,x2,x3,x4;
  int             mbs=a->mbs,i,*idx,*ii,j,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = 0.0; sum2 = 0.0; sum3 = 0.0; sum4 = 0.0;
    for ( j=0; j<n; j++ ) {
      xb = x + 4*(*idx++);
      x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; x4 = xb[3];
      sum1 += v[0]*x1 + v[4]*x2 + v[8]*x3   + v[12]*x4;
      sum2 += v[1]*x1 + v[5]*x2 + v[9]*x3   + v[13]*x4;
      sum3 += v[2]*x1 + v[6]*x2 + v[10]*x3  + v[14]*x4;
      sum4 += v[3]*x1 + v[7]*x2 + v[11]*x3  + v[15]*x4;
      v += 16;
    }
    z[0] = sum1; z[1] = sum2; z[2] = sum3; z[3] = sum4;
    z += 4;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(32*a->nz - a->m);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMult_SeqBAIJ_5"
int MatMult_SeqBAIJ_5(Mat A,Vec xx,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar sum1,sum2,sum3,sum4,sum5,x1,x2,x3,x4,x5;
  register Scalar * restrict v,* restrict xb,* restrict z, * restrict x;
  int             mbs=a->mbs,i,*idx,*ii,j,n;

  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = 0.0; sum2 = 0.0; sum3 = 0.0; sum4 = 0.0; sum5 = 0.0;
    for ( j=0; j<n; j++ ) {
      xb = x + 5*(*idx++);
      x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; x4 = xb[3]; x5 = xb[4];
      sum1 += v[0]*x1 + v[5]*x2 + v[10]*x3  + v[15]*x4 + v[20]*x5;
      sum2 += v[1]*x1 + v[6]*x2 + v[11]*x3  + v[16]*x4 + v[21]*x5;
      sum3 += v[2]*x1 + v[7]*x2 + v[12]*x3  + v[17]*x4 + v[22]*x5;
      sum4 += v[3]*x1 + v[8]*x2 + v[13]*x3  + v[18]*x4 + v[23]*x5;
      sum5 += v[4]*x1 + v[9]*x2 + v[14]*x3  + v[19]*x4 + v[24]*x5;
      v += 25;
    }
    z[0] = sum1; z[1] = sum2; z[2] = sum3; z[3] = sum4; z[4] = sum5;
    z += 5;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(50*a->nz - a->m);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMult_SeqBAIJ_7"
int MatMult_SeqBAIJ_7(Mat A,Vec xx,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*z,*v,*xb,sum1,sum2,sum3,sum4,sum5,sum6,sum7;
  register Scalar x1,x2,x3,x4,x5,x6,x7;
  int             mbs=a->mbs,i,*idx,*ii,j,n;

  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = 0.0; sum2 = 0.0; sum3 = 0.0; sum4 = 0.0; sum5 = 0.0; sum6 = 0.0; sum7 = 0.0;
    for ( j=0; j<n; j++ ) {
      xb = x + 7*(*idx++);
      x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; x4 = xb[3]; x5 = xb[4]; x6 = xb[5]; x7 = xb[6];
      sum1 += v[0]*x1 + v[7]*x2  + v[14]*x3  + v[21]*x4 + v[28]*x5 + v[35]*x6 + v[42]*x7;
      sum2 += v[1]*x1 + v[8]*x2  + v[15]*x3  + v[22]*x4 + v[29]*x5 + v[36]*x6 + v[43]*x7;
      sum3 += v[2]*x1 + v[9]*x2  + v[16]*x3  + v[23]*x4 + v[30]*x5 + v[37]*x6 + v[44]*x7;
      sum4 += v[3]*x1 + v[10]*x2 + v[17]*x3  + v[24]*x4 + v[31]*x5 + v[38]*x6 + v[45]*x7;
      sum5 += v[4]*x1 + v[11]*x2 + v[18]*x3  + v[25]*x4 + v[32]*x5 + v[39]*x6 + v[46]*x7;
      sum6 += v[5]*x1 + v[12]*x2 + v[19]*x3  + v[26]*x4 + v[33]*x5 + v[40]*x6 + v[47]*x7;
      sum7 += v[6]*x1 + v[13]*x2 + v[20]*x3  + v[27]*x4 + v[34]*x5 + v[41]*x6 + v[48]*x7;
      v += 49;
    }
    z[0] = sum1; z[1] = sum2; z[2] = sum3; z[3] = sum4; z[4] = sum5; z[5] = sum6; z[6] = sum7;
    z += 7;
  }

  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(98*a->nz - a->m);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMult_SeqBAIJ_N"
int MatMult_SeqBAIJ_N(Mat A,Vec xx,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*z,*v,*xb;
  Scalar          _DOne = 1.0,*work,*workt,_DZero = 0.0;
  int             mbs=a->mbs,i,*idx,*ii,bs=a->bs,j,n,bs2=a->bs2;
  int             _One = 1,ncols,k;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;


  if (!a->mult_work) {
    k = PetscMax(a->m,a->n);
    a->mult_work = (Scalar *) PetscMalloc((k+1)*sizeof(Scalar));CHKPTRQ(a->mult_work);
  }
  work = a->mult_work;
  for ( i=0; i<mbs; i++ ) {
    n     = ii[1] - ii[0]; ii++;
    ncols = n*bs;
    workt = work;
    for ( j=0; j<n; j++ ) {
      xb = x + bs*(*idx++);
      for ( k=0; k<bs; k++ ) workt[k] = xb[k];
      workt += bs;
    }
    LAgemv_("N",&bs,&ncols,&_DOne,v,&bs,work,&_One,&_DZero,z,&_One);
    v += n*bs2;
    z += bs;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(2*a->nz*bs2 - a->m);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMultAdd_SeqBAIJ_1"
int MatMultAdd_SeqBAIJ_1(Mat A,Vec xx,Vec yy,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*y,*z,*v,sum;
  int             mbs=a->mbs,i,*idx,*ii,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(yy,y);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n    = ii[1] - ii[0]; ii++;
    sum  = y[i];
    while (n--) sum += *v++ * x[*idx++];
    z[i] = sum;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(yy,y);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(2*a->nz);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMultAdd_SeqBAIJ_2"
int MatMultAdd_SeqBAIJ_2(Mat A,Vec xx,Vec yy,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*y,*z,*v,*xb,sum1,sum2;
  register Scalar x1,x2;
  int             mbs=a->mbs,i,*idx,*ii,j,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(yy,y);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = y[0]; sum2 = y[1];
    for ( j=0; j<n; j++ ) {
      xb = x + 2*(*idx++); x1 = xb[0]; x2 = xb[1];
      sum1 += v[0]*x1 + v[2]*x2;
      sum2 += v[1]*x1 + v[3]*x2;
      v += 4;
    }
    z[0] = sum1; z[1] = sum2;
    z += 2; y += 2;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(yy,y);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(4*a->nz);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMultAdd_SeqBAIJ_3"
int MatMultAdd_SeqBAIJ_3(Mat A,Vec xx,Vec yy,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*y,*z,*v,*xb,sum1,sum2,sum3,x1,x2,x3;
  int             mbs=a->mbs,i,*idx,*ii,j,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(yy,y);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = y[0]; sum2 = y[1]; sum3 = y[2];
    for ( j=0; j<n; j++ ) {
      xb = x + 3*(*idx++); x1 = xb[0]; x2 = xb[1]; x3 = xb[2];
      sum1 += v[0]*x1 + v[3]*x2 + v[6]*x3;
      sum2 += v[1]*x1 + v[4]*x2 + v[7]*x3;
      sum3 += v[2]*x1 + v[5]*x2 + v[8]*x3;
      v += 9;
    }
    z[0] = sum1; z[1] = sum2; z[2] = sum3;
    z += 3; y += 3;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(yy,y);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(18*a->nz);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMultAdd_SeqBAIJ_4"
int MatMultAdd_SeqBAIJ_4(Mat A,Vec xx,Vec yy,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*y,*z,*v,*xb,sum1,sum2,sum3,sum4,x1,x2,x3,x4;
  int             mbs=a->mbs,i,*idx,*ii;
  int             j,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(yy,y);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = y[0]; sum2 = y[1]; sum3 = y[2]; sum4 = y[3];
    for ( j=0; j<n; j++ ) {
      xb = x + 4*(*idx++);
      x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; x4 = xb[3];
      sum1 += v[0]*x1 + v[4]*x2 + v[8]*x3   + v[12]*x4;
      sum2 += v[1]*x1 + v[5]*x2 + v[9]*x3   + v[13]*x4;
      sum3 += v[2]*x1 + v[6]*x2 + v[10]*x3  + v[14]*x4;
      sum4 += v[3]*x1 + v[7]*x2 + v[11]*x3  + v[15]*x4;
      v += 16;
    }
    z[0] = sum1; z[1] = sum2; z[2] = sum3; z[3] = sum4;
    z += 4; y += 4;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(yy,y);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(32*a->nz);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMultAdd_SeqBAIJ_5"
int MatMultAdd_SeqBAIJ_5(Mat A,Vec xx,Vec yy,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*y,*z,*v,*xb,sum1,sum2,sum3,sum4,sum5,x1,x2,x3,x4,x5;
  int             mbs=a->mbs,i,*idx,*ii,j,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(yy,y);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = y[0]; sum2 = y[1]; sum3 = y[2]; sum4 = y[3]; sum5 = y[4];
    for ( j=0; j<n; j++ ) {
      xb = x + 5*(*idx++);
      x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; x4 = xb[3]; x5 = xb[4];
      sum1 += v[0]*x1 + v[5]*x2 + v[10]*x3  + v[15]*x4 + v[20]*x5;
      sum2 += v[1]*x1 + v[6]*x2 + v[11]*x3  + v[16]*x4 + v[21]*x5;
      sum3 += v[2]*x1 + v[7]*x2 + v[12]*x3  + v[17]*x4 + v[22]*x5;
      sum4 += v[3]*x1 + v[8]*x2 + v[13]*x3  + v[18]*x4 + v[23]*x5;
      sum5 += v[4]*x1 + v[9]*x2 + v[14]*x3  + v[19]*x4 + v[24]*x5;
      v += 25;
    }
    z[0] = sum1; z[1] = sum2; z[2] = sum3; z[3] = sum4; z[4] = sum5;
    z += 5; y += 5;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(yy,y);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(50*a->nz);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMultAdd_SeqBAIJ_7"
int MatMultAdd_SeqBAIJ_7(Mat A,Vec xx,Vec yy,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*y,*z,*v,*xb,sum1,sum2,sum3,sum4,sum5,sum6,sum7;
  register Scalar x1,x2,x3,x4,x5,x6,x7;
  int             mbs=a->mbs,i,*idx,*ii,j,n;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(yy,y);
  VecGetArray_Fast(zz,z);

  idx   = a->j;
  v     = a->a;
  ii    = a->i;

  for ( i=0; i<mbs; i++ ) {
    n  = ii[1] - ii[0]; ii++; 
    sum1 = y[0]; sum2 = y[1]; sum3 = y[2]; sum4 = y[3]; sum5 = y[4]; sum6 = y[5]; sum7 = y[6];
    for ( j=0; j<n; j++ ) {
      xb = x + 7*(*idx++);
      x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; x4 = xb[3]; x5 = xb[4]; x6 = xb[5]; x7 = xb[6];
      sum1 += v[0]*x1 + v[7]*x2  + v[14]*x3  + v[21]*x4 + v[28]*x5 + v[35]*x6 + v[42]*x7;
      sum2 += v[1]*x1 + v[8]*x2  + v[15]*x3  + v[22]*x4 + v[29]*x5 + v[36]*x6 + v[43]*x7;
      sum3 += v[2]*x1 + v[9]*x2  + v[16]*x3  + v[23]*x4 + v[30]*x5 + v[37]*x6 + v[44]*x7;
      sum4 += v[3]*x1 + v[10]*x2 + v[17]*x3  + v[24]*x4 + v[31]*x5 + v[38]*x6 + v[45]*x7;
      sum5 += v[4]*x1 + v[11]*x2 + v[18]*x3  + v[25]*x4 + v[32]*x5 + v[39]*x6 + v[46]*x7;
      sum6 += v[5]*x1 + v[12]*x2 + v[19]*x3  + v[26]*x4 + v[33]*x5 + v[40]*x6 + v[47]*x7;
      sum7 += v[6]*x1 + v[13]*x2 + v[20]*x3  + v[27]*x4 + v[34]*x5 + v[41]*x6 + v[48]*x7;
      v += 49;
    }
    z[0] = sum1; z[1] = sum2; z[2] = sum3; z[3] = sum4; z[4] = sum5; z[5] = sum6; z[6] = sum7;
    z += 7; y += 7;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(yy,y);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(98*a->nz);
  PetscFunctionReturn(0);
}


#undef __FUNC__  
#define __FUNC__ "MatMultAdd_SeqBAIJ_N"
int MatMultAdd_SeqBAIJ_N(Mat A,Vec xx,Vec yy,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  register Scalar *x,*z,*v,*xb;
  int             mbs=a->mbs,i,*idx,*ii,bs=a->bs,j,n,bs2=a->bs2,ierr;
  int             _One = 1,ncols,k; Scalar _DOne = 1.0, *work,*workt;

  PetscFunctionBegin;
  if ( xx != yy) { ierr = VecCopy(yy,zz); CHKERRQ(ierr); }

  VecGetArray_Fast(xx,x);
  VecGetArray_Fast(zz,z);
 
  idx   = a->j;
  v     = a->a;
  ii    = a->i;


  if (!a->mult_work) {
    k = PetscMax(a->m,a->n);
    a->mult_work = (Scalar *) PetscMalloc(k*sizeof(Scalar));CHKPTRQ(a->mult_work);
  }
  work = a->mult_work;
  for ( i=0; i<mbs; i++ ) {
    n     = ii[1] - ii[0]; ii++;
    ncols = n*bs;
    workt = work;
    for ( j=0; j<n; j++ ) {
      xb = x + bs*(*idx++);
      for ( k=0; k<bs; k++ ) workt[k] = xb[k];
      workt += bs;
    }
    LAgemv_("N",&bs,&ncols,&_DOne,v,&bs,work,&_One,&_DOne,z,&_One);
    v += n*bs2;
    z += bs;
  }
  VecRestoreArray_Fast(xx,x);
  VecRestoreArray_Fast(zz,z);
  PLogFlops(2*a->nz*bs2 );
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMultTrans_SeqBAIJ"
int MatMultTrans_SeqBAIJ(Mat A,Vec xx,Vec zz)
{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  Scalar          *xg,*zg,*zb;
  register Scalar *x,*z,*v,*xb,x1,x2,x3,x4,x5;
  int             mbs=a->mbs,i,*idx,*ii,*ai=a->i,rval,N=a->n;
  int             bs=a->bs,j,n,bs2=a->bs2,*ib,ierr;


  PetscFunctionBegin;
  VecGetArray_Fast(xx,xg); x = xg;
  VecGetArray_Fast(zz,zg); z = zg;
  PetscMemzero(z,N*sizeof(Scalar));

  idx   = a->j;
  v     = a->a;
  ii    = a->i;
  
  switch (bs) {
  case 1:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++;
      xb = x + i; x1 = xb[0];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval    = ib[j];
        z[rval] += *v++ * x1;
      }
    }
    break;
  case 2:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++; 
      xb = x + 2*i; x1 = xb[0]; x2 = xb[1];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval      = ib[j]*2;
        z[rval++] += v[0]*x1 + v[1]*x2;
        z[rval++] += v[2]*x1 + v[3]*x2;
        v += 4;
      }
    }
    break;
  case 3:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++; 
      xb = x + 3*i; x1 = xb[0]; x2 = xb[1]; x3 = xb[2];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval      = ib[j]*3;
        z[rval++] += v[0]*x1 + v[1]*x2 + v[2]*x3;
        z[rval++] += v[3]*x1 + v[4]*x2 + v[5]*x3;
        z[rval++] += v[6]*x1 + v[7]*x2 + v[8]*x3;
        v += 9;
      }
    }
    break;
  case 4:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++; 
      xb = x + 4*i; x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; x4 = xb[3];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval      = ib[j]*4;
        z[rval++] +=  v[0]*x1 +  v[1]*x2 +  v[2]*x3 +  v[3]*x4;
        z[rval++] +=  v[4]*x1 +  v[5]*x2 +  v[6]*x3 +  v[7]*x4;
        z[rval++] +=  v[8]*x1 +  v[9]*x2 + v[10]*x3 + v[11]*x4;
        z[rval++] += v[12]*x1 + v[13]*x2 + v[14]*x3 + v[15]*x4;
        v += 16;
      }
    }
    break;
  case 5:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++; 
      xb = x + 5*i; x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; 
      x4 = xb[3];   x5 = xb[4];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval      = ib[j]*5;
        z[rval++] +=  v[0]*x1 +  v[1]*x2 +  v[2]*x3 +  v[3]*x4 +  v[4]*x5;
        z[rval++] +=  v[5]*x1 +  v[6]*x2 +  v[7]*x3 +  v[8]*x4 +  v[9]*x5;
        z[rval++] += v[10]*x1 + v[11]*x2 + v[12]*x3 + v[13]*x4 + v[14]*x5;
        z[rval++] += v[15]*x1 + v[16]*x2 + v[17]*x3 + v[18]*x4 + v[19]*x5;
        z[rval++] += v[20]*x1 + v[21]*x2 + v[22]*x3 + v[23]*x4 + v[24]*x5;
        v += 25;
      }
    }
    break;
      /* block sizes larger then 5 by 5 are handled by BLAS */
    default: {
      int  _One = 1,ncols,k; Scalar _DOne = 1.0, *work,*workt;
      if (!a->mult_work) {
        k = PetscMax(a->m,a->n);
        a->mult_work = (Scalar *) PetscMalloc(k*sizeof(Scalar));
        CHKPTRQ(a->mult_work);
      }
      work = a->mult_work;
      for ( i=0; i<mbs; i++ ) {
        n     = ii[1] - ii[0]; ii++;
        ncols = n*bs;
        PetscMemzero(work,ncols*sizeof(Scalar));
        LAgemv_("T",&bs,&ncols,&_DOne,v,&bs,x,&_One,&_DOne,work,&_One);
        v += n*bs2;
        x += bs;
        workt = work;
        for ( j=0; j<n; j++ ) {
          zb = z + bs*(*idx++);
          for ( k=0; k<bs; k++ ) zb[k] += workt[k] ;
          workt += bs;
        }
      }
    }
  }
  ierr = VecRestoreArray(xx,&xg); CHKERRQ(ierr);
  ierr = VecRestoreArray(zz,&zg); CHKERRQ(ierr);
  PLogFlops(2*a->nz*a->bs2 - a->n);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatMultTransAdd_SeqBAIJ"
int MatMultTransAdd_SeqBAIJ(Mat A,Vec xx,Vec yy,Vec zz)

{
  Mat_SeqBAIJ     *a = (Mat_SeqBAIJ *) A->data;
  Scalar          *xg,*zg,*zb;
  register Scalar *x,*z,*v,*xb,x1,x2,x3,x4,x5;
  int             mbs=a->mbs,i,*idx,*ii,*ai=a->i,rval,N=a->n;
  int             bs=a->bs,j,n,bs2=a->bs2,*ib,ierr;

  PetscFunctionBegin;
  VecGetArray_Fast(xx,xg); x = xg;
  VecGetArray_Fast(zz,zg); z = zg;

  if ( yy != zz ) { ierr = VecCopy(yy,zz); CHKERRQ(ierr); }
  else PetscMemzero(z,N*sizeof(Scalar));

  idx   = a->j;
  v     = a->a;
  ii    = a->i;
  
  switch (bs) {
  case 1:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++;
      xb = x + i; x1 = xb[0];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval    = ib[j];
        z[rval] += *v++ * x1;
      }
    }
    break;
  case 2:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++; 
      xb = x + 2*i; x1 = xb[0]; x2 = xb[1];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval      = ib[j]*2;
        z[rval++] += v[0]*x1 + v[1]*x2;
        z[rval++] += v[2]*x1 + v[3]*x2;
        v += 4;
      }
    }
    break;
  case 3:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++; 
      xb = x + 3*i; x1 = xb[0]; x2 = xb[1]; x3 = xb[2];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval      = ib[j]*3;
        z[rval++] += v[0]*x1 + v[1]*x2 + v[2]*x3;
        z[rval++] += v[3]*x1 + v[4]*x2 + v[5]*x3;
        z[rval++] += v[6]*x1 + v[7]*x2 + v[8]*x3;
        v += 9;
      }
    }
    break;
  case 4:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++; 
      xb = x + 4*i; x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; x4 = xb[3];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval      = ib[j]*4;
        z[rval++] +=  v[0]*x1 +  v[1]*x2 +  v[2]*x3 +  v[3]*x4;
        z[rval++] +=  v[4]*x1 +  v[5]*x2 +  v[6]*x3 +  v[7]*x4;
        z[rval++] +=  v[8]*x1 +  v[9]*x2 + v[10]*x3 + v[11]*x4;
        z[rval++] += v[12]*x1 + v[13]*x2 + v[14]*x3 + v[15]*x4;
        v += 16;
      }
    }
    break;
  case 5:
    for ( i=0; i<mbs; i++ ) {
      n  = ii[1] - ii[0]; ii++; 
      xb = x + 5*i; x1 = xb[0]; x2 = xb[1]; x3 = xb[2]; 
      x4 = xb[3];   x5 = xb[4];
      ib = idx + ai[i];
      for ( j=0; j<n; j++ ) {
        rval      = ib[j]*5;
        z[rval++] +=  v[0]*x1 +  v[1]*x2 +  v[2]*x3 +  v[3]*x4 +  v[4]*x5;
        z[rval++] +=  v[5]*x1 +  v[6]*x2 +  v[7]*x3 +  v[8]*x4 +  v[9]*x5;
        z[rval++] += v[10]*x1 + v[11]*x2 + v[12]*x3 + v[13]*x4 + v[14]*x5;
        z[rval++] += v[15]*x1 + v[16]*x2 + v[17]*x3 + v[18]*x4 + v[19]*x5;
        z[rval++] += v[20]*x1 + v[21]*x2 + v[22]*x3 + v[23]*x4 + v[24]*x5;
        v += 25;
      }
    }
    break;
      /* block sizes larger then 5 by 5 are handled by BLAS */
    default: {
      int  _One = 1,ncols,k; Scalar _DOne = 1.0, *work,*workt;
      if (!a->mult_work) {
        k = PetscMax(a->m,a->n);
        a->mult_work = (Scalar *) PetscMalloc(k*sizeof(Scalar));
        CHKPTRQ(a->mult_work);
      }
      work = a->mult_work;
      for ( i=0; i<mbs; i++ ) {
        n     = ii[1] - ii[0]; ii++;
        ncols = n*bs;
        PetscMemzero(work,ncols*sizeof(Scalar));
        LAgemv_("T",&bs,&ncols,&_DOne,v,&bs,x,&_One,&_DOne,work,&_One);
        v += n*bs2;
        x += bs;
        workt = work;
        for ( j=0; j<n; j++ ) {
          zb = z + bs*(*idx++);
          for ( k=0; k<bs; k++ ) zb[k] += workt[k] ;
          workt += bs;
        }
      }
    }
  }
  ierr = VecRestoreArray(xx,&xg); CHKERRQ(ierr);
  ierr = VecRestoreArray(zz,&zg); CHKERRQ(ierr);
  PLogFlops(2*a->nz*a->bs2);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatScale_SeqBAIJ"
int MatScale_SeqBAIJ(Scalar *alpha,Mat inA)
{
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ *) inA->data;
  int         one = 1, totalnz = a->bs2*a->nz;

  PetscFunctionBegin;
  BLscal_( &totalnz, alpha, a->a, &one );
  PLogFlops(totalnz);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatNorm_SeqBAIJ"
int MatNorm_SeqBAIJ(Mat A,NormType type,double *norm)
{
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ *) A->data;
  Scalar      *v = a->a;
  double      sum = 0.0;
  int         i,nz=a->nz,bs2=a->bs2;

  PetscFunctionBegin;
  if (type == NORM_FROBENIUS) {
    for (i=0; i< bs2*nz; i++ ) {
#if defined(USE_PETSC_COMPLEX)
      sum += real(conj(*v)*(*v)); v++;
#else
      sum += (*v)*(*v); v++;
#endif
    }
    *norm = sqrt(sum);
  }
  else {
    SETERRQ(PETSC_ERR_SUP,0,"No support for this norm yet");
  }
  PetscFunctionReturn(0);
}


#undef __FUNC__  
#define __FUNC__ "MatEqual_SeqBAIJ"
int MatEqual_SeqBAIJ(Mat A,Mat B, PetscTruth* flg)
{
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ *)A->data, *b = (Mat_SeqBAIJ *)B->data;

  PetscFunctionBegin;
  if (B->type !=MATSEQBAIJ) SETERRQ(PETSC_ERR_ARG_INCOMP,0,"Matrices must be same type");

  /* If the  matrix/block dimensions are not equal, or no of nonzeros or shift */
  if ((a->m != b->m) || (a->n !=b->n) || (a->bs != b->bs)|| (a->nz != b->nz)) {
    *flg = PETSC_FALSE; PetscFunctionReturn(0); 
  }
  
  /* if the a->i are the same */
  if (PetscMemcmp(a->i,b->i, (a->mbs+1)*sizeof(int))) { 
    *flg = PETSC_FALSE; PetscFunctionReturn(0);
  }
  
  /* if a->j are the same */
  if (PetscMemcmp(a->j,b->j,(a->nz)*sizeof(int))) { 
    *flg = PETSC_FALSE; PetscFunctionReturn(0);
  }
  
  /* if a->a are the same */
  if (PetscMemcmp(a->a, b->a,(a->nz)*(a->bs)*(a->bs)*sizeof(Scalar))) {
    *flg = PETSC_FALSE; PetscFunctionReturn(0);
  }
  *flg = PETSC_TRUE; 
  PetscFunctionReturn(0);
  
}

#undef __FUNC__  
#define __FUNC__ "MatGetDiagonal_SeqBAIJ"
int MatGetDiagonal_SeqBAIJ(Mat A,Vec v)
{
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ *) A->data;
  int         i,j,k,n,row,bs,*ai,*aj,ambs,bs2;
  Scalar      *x, zero = 0.0,*aa,*aa_j;

  PetscFunctionBegin;
  if (A->factor) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"Not for factored matrix");  
  bs   = a->bs;
  aa   = a->a;
  ai   = a->i;
  aj   = a->j;
  ambs = a->mbs;
  bs2  = a->bs2;

  VecSet(&zero,v);
  VecGetArray_Fast(v,x); VecGetLocalSize_Fast(v,n);
  if (n != a->m) SETERRQ(PETSC_ERR_ARG_SIZ,0,"Nonconforming matrix and vector");
  for ( i=0; i<ambs; i++ ) {
    for ( j=ai[i]; j<ai[i+1]; j++ ) {
      if (aj[j] == i) {
        row  = i*bs;
        aa_j = aa+j*bs2;
        for (k=0; k<bs2; k+=(bs+1),row++) x[row] = aa_j[k];
        break;
      }
    }
  }
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatDiagonalScale_SeqBAIJ"
int MatDiagonalScale_SeqBAIJ(Mat A,Vec ll,Vec rr)
{
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ *) A->data;
  Scalar      *l,*r,x,*v,*aa,*li,*ri;
  int         i,j,k,lm,rn,M,m,n,*ai,*aj,mbs,tmp,bs,bs2;

  PetscFunctionBegin;
  ai  = a->i;
  aj  = a->j;
  aa  = a->a;
  m   = a->m;
  n   = a->n;
  bs  = a->bs;
  mbs = a->mbs;
  bs2 = a->bs2;
  if (ll) {
    VecGetArray_Fast(ll,l); VecGetLocalSize_Fast(ll,lm);
    if (lm != m) SETERRQ(PETSC_ERR_ARG_SIZ,0,"Left scaling vector wrong length");
    for ( i=0; i<mbs; i++ ) { /* for each block row */
      M  = ai[i+1] - ai[i];
      li = l + i*bs;
      v  = aa + bs2*ai[i];
      for ( j=0; j<M; j++ ) { /* for each block */
        for ( k=0; k<bs2; k++ ) {
          (*v++) *= li[k%bs];
        } 
      }  
    }
  }
  
  if (rr) {
    VecGetArray_Fast(rr,r); VecGetLocalSize_Fast(rr,rn);
    if (rn != n) SETERRQ(PETSC_ERR_ARG_SIZ,0,"Right scaling vector wrong length");
    for ( i=0; i<mbs; i++ ) { /* for each block row */
      M  = ai[i+1] - ai[i];
      v  = aa + bs2*ai[i];
      for ( j=0; j<M; j++ ) { /* for each block */
        ri = r + bs*aj[ai[i]+j];
        for ( k=0; k<bs; k++ ) {
          x = ri[k];
          for ( tmp=0; tmp<bs; tmp++ ) (*v++) *= x;
        } 
      }  
    }
  }
  PetscFunctionReturn(0);
}


#undef __FUNC__  
#define __FUNC__ "MatGetInfo_SeqBAIJ"
int MatGetInfo_SeqBAIJ(Mat A,MatInfoType flag,MatInfo *info)
{
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ *) A->data;

  PetscFunctionBegin;
  info->rows_global    = (double)a->m;
  info->columns_global = (double)a->n;
  info->rows_local     = (double)a->m;
  info->columns_local  = (double)a->n;
  info->block_size     = a->bs2;
  info->nz_allocated   = a->maxnz;
  info->nz_used        = a->bs2*a->nz;
  info->nz_unneeded    = (double)(info->nz_allocated - info->nz_used);
  /*  if (info->nz_unneeded != A->info.nz_unneeded) 
    printf("space descrepancy: maxnz-nz = %d, nz_unneeded = %d\n",(int)info->nz_unneeded,(int)A->info.nz_unneeded); */
  info->assemblies   = A->num_ass;
  info->mallocs      = a->reallocs;
  info->memory       = A->mem;
  if (A->factor) {
    info->fill_ratio_given  = A->info.fill_ratio_given;
    info->fill_ratio_needed = A->info.fill_ratio_needed;
    info->factor_mallocs    = A->info.factor_mallocs;
  } else {
    info->fill_ratio_given  = 0;
    info->fill_ratio_needed = 0;
    info->factor_mallocs    = 0;
  }
  PetscFunctionReturn(0);
}


#undef __FUNC__  
#define __FUNC__ "MatZeroEntries_SeqBAIJ"
int MatZeroEntries_SeqBAIJ(Mat A)
{
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ *) A->data; 

  PetscFunctionBegin;
  PetscMemzero(a->a,a->bs2*a->i[a->mbs]*sizeof(Scalar));
  PetscFunctionReturn(0);
}
