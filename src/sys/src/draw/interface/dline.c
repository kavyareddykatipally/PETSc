#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: dline.c,v 1.13 1997/10/19 03:27:39 bsmith Exp bsmith $";
#endif
/*
       Provides the calling sequences for all the basic Draw routines.
*/
#include "src/draw/drawimpl.h"  /*I "draw.h" I*/
  
#undef __FUNC__  
#define __FUNC__ "DrawLine" 
/*@
   DrawLine - Draws a line onto a drawable.

   Input Parameters:
.   draw - the drawing context
.   xl,yl,xr,yr - the coordinates of the line endpoints
.   cl - the colors of the endpoints

.keywords:  draw, line
@*/
int DrawLine(Draw draw,double xl,double yl,double xr,double yr,int cl)
{
  int ierr;
  PetscFunctionBegin;
  PetscValidHeaderSpecific(draw,DRAW_COOKIE);
  if (draw->type == DRAW_NULLWINDOW) PetscFunctionReturn(0);
  ierr = (*draw->ops->line)(draw,xl,yl,xr,yr,cl);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "DrawIsNull" 
/*@
    DrawIsNull - Returns PETSC_TRUE if draw is a null draw object.

  Input Parameter:
    draw - the draw context

  Output Parameter:
    yes - PETSC_TRUE if it is a null draw object, else PETSC_FALSE

@*/
int DrawIsNull(Draw draw,PetscTruth *yes)
{
  PetscFunctionBegin;
  if (draw->type == DRAW_NULLWINDOW) *yes = PETSC_TRUE;
  else                               *yes = PETSC_FALSE;
  PetscFunctionReturn(0);
}
