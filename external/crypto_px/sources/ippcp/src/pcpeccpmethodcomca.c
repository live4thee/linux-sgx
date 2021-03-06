/*
* Copyright (C) 2016 Intel Corporation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in
*     the documentation and/or other materials provided with the
*     distribution.
*   * Neither the name of Intel Corporation nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
* A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
* OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#include "owndefs.h"
#include "owncp.h"
#include "pcpeccppoint.h"
#include "pcpeccpmethod.h"
#include "pcpeccpmethodcom.h"
#include "pcppma.h"
#include "pcpeccpsscm.h"


static
ECCP_METHOD ECCPcom = {
   ECCP_SetPointProjective,
   ECCP_SetPointAffine,
   ECCP_GetPointAffine,

   ECCP_IsPointOnCurve,

   ECCP_ComparePoint,
   ECCP_NegPoint,
   ECCP_DblPoint,
   ECCP_AddPoint,
   ECCP_MulPoint,
   ECCP_MulBasePoint,
   ECCP_ProdPoint
};


/*
// Returns reference
*/
ECCP_METHOD* ECCPcom_Methods(void)
{
   return &ECCPcom;
}


/*
// Copy Point
*/
void ECCP_CopyPoint(const IppsECCPPointState* pSrc, IppsECCPPointState* pDst)
{
   cpBN_copy(ECP_POINT_X(pDst), ECP_POINT_X(pSrc));
   cpBN_copy(ECP_POINT_Y(pDst), ECP_POINT_Y(pSrc));
   cpBN_copy(ECP_POINT_Z(pDst), ECP_POINT_Z(pSrc));
   ECP_POINT_AFFINE(pDst) = ECP_POINT_AFFINE(pSrc);
}

/*
// ECCP_PoinSettProjective
// Converts regular projective triplet (pX,pY,pZ) into pPoint
// (see note above)
*/
void ECCP_SetPointProjective(const IppsBigNumState* pX,
                             const IppsBigNumState* pY,
                             const IppsBigNumState* pZ,
                             IppsECCPPointState* pPoint,
                             const IppsECCPState* pECC)
{
   IppsMontState* pMont = ECP_PMONT(pECC);

   PMA_enc(ECP_POINT_X(pPoint), (IppsBigNumState*)pX, pMont);
   PMA_enc(ECP_POINT_Y(pPoint), (IppsBigNumState*)pY, pMont);
   PMA_enc(ECP_POINT_Z(pPoint), (IppsBigNumState*)pZ, pMont);
   ECP_POINT_AFFINE(pPoint) = cpBN_cmp(pZ, BN_ONE_REF())==0;
}

/*
// ECCP_PointAffineSet
// Converts regular affine pair (pX,pY) into pPoint
*/
void ECCP_SetPointAffine(const IppsBigNumState* pX,
                         const IppsBigNumState* pY,
                         IppsECCPPointState* pPoint,
                         const IppsECCPState* pECC)
{
   IppsMontState* pMont = ECP_PMONT(pECC);
   PMA_enc(ECP_POINT_X(pPoint), (IppsBigNumState*)pX, pMont);
   PMA_enc(ECP_POINT_Y(pPoint), (IppsBigNumState*)pY, pMont);
   PMA_enc(ECP_POINT_Z(pPoint), (IppsBigNumState*)cpBN_OneRef(), pMont);
   ECP_POINT_AFFINE(pPoint) = 1;
}

/*
// ECCP_GetPointAffine
//
// Converts pPoint into regular affine pair (pX,pY)
//
// Note:
// pPoint is not point at Infinity
// transform  (X, Y, Z)  into  (x, y) = (X/Z^2, Y/Z^3)
*/
void ECCP_GetPointAffine(IppsBigNumState* pX, IppsBigNumState* pY,
                         const IppsECCPPointState* pPoint,
                         const IppsECCPState* pECC,
                         BigNumNode* pList)
{
   IppsMontState* pMont = ECP_PMONT(pECC);

   /* case Z == 1 */
   if( ECP_POINT_AFFINE(pPoint) ) {
      if(pX) {
         PMA_dec(pX, ECP_POINT_X(pPoint), pMont);
      }
      if(pY) {
         PMA_dec(pY, ECP_POINT_Y(pPoint), pMont);
      }
   }

   /* case Z != 1 */
   else {
      IppsBigNumState* pT = cpBigNumListGet(&pList);
      IppsBigNumState* pU = cpBigNumListGet(&pList);
      IppsBigNumState* pModulo = ECP_PRIME(pECC);

      /* decode Z */
      PMA_dec(pU, ECP_POINT_Z(pPoint), pMont);
      /* regular T = Z^-1 */
      PMA_inv(pT, pU, pModulo);
      /* montgomery U = Z^-1 */
      PMA_enc(pU, pT, pMont);
      /* regular T = Z^-2 */
      PMA_mule(pT, pU, pT, pMont);

      if(pX) {
         PMA_mule(pX,pT, ECP_POINT_X(pPoint), pMont);
      }
      if(pY) {
         /* regular U = Z^-3 */
         PMA_mule(pU, pU, pT, pMont);
         PMA_mule(pY,pU, ECP_POINT_Y(pPoint), pMont);
      }
   }
}

/*
// ECCP_SetPointToInfinity
// ECCP_SetPointToAffineInfinity0
// ECCP_SetPointToAffineInfinity1
//
// Set point to Infinity
*/
void ECCP_SetPointToInfinity(IppsECCPPointState* pPoint)
{
   cpBN_zero(ECP_POINT_X(pPoint));
   cpBN_zero(ECP_POINT_Y(pPoint));
   cpBN_zero(ECP_POINT_Z(pPoint));
   ECP_POINT_AFFINE(pPoint) = 0;
}

void ECCP_SetPointToAffineInfinity0(IppsBigNumState* pX, IppsBigNumState* pY)
{
   if(pX) cpBN_zero(pX);
   if(pY) cpBN_zero(pY);
}

void ECCP_SetPointToAffineInfinity1(IppsBigNumState* pX, IppsBigNumState* pY)
{
   if(pX) cpBN_zero(pX);
   if(pY) BN_Word(pY,1);
}

/*
// ECCP_IsPointAtInfinity
// ECCP_IsPointAtAffineInfinity0
// ECCP_IsPointAtAffineInfinity1
//
// Test point is at Infinity
*/
int ECCP_IsPointAtInfinity(const IppsECCPPointState* pPoint)
{
   return IsZero_BN( ECP_POINT_Z(pPoint) );
}

int ECCP_IsPointAtAffineInfinity0(const IppsBigNumState* pX, const IppsBigNumState* pY)
{
   return IsZero_BN(pX) && IsZero_BN(pY);
}

int ECCP_IsPointAtAffineInfinity1(const IppsBigNumState* pX, const IppsBigNumState* pY)
{
   return IsZero_BN(pX) && !IsZero_BN(pY);
}

/*
// ECCP_IsPointOnCurve
//
// Test point is lie on curve
//
// Note
// We deal with equation: y^2 = x^3 + A*x + B.
// Or in projective coordinates: Y^2 = X^3 + a*X*Z^4 + b*Z^6.
// The point under test is given by projective triplet (X,Y,Z),
// which represents actually (x,y) = (X/Z^2,Y/Z^3).
*/
int ECCP_IsPointOnCurve(const IppsECCPPointState* pPoint,
                        const IppsECCPState* pECC,
                        BigNumNode* pList)
{
   /* let think Infinity point is on the curve */
   if( ECCP_IsPointAtInfinity(pPoint) )
      return 1;

   else {
      IppsMontState*   pMont = ECP_PMONT(pECC);
      IppsBigNumState* pR = cpBigNumListGet(&pList);
      IppsBigNumState* pT = cpBigNumListGet(&pList);
      IppsBigNumState* pModulo = ECP_PRIME(pECC);

      PMA_sqre(pR, ECP_POINT_X(pPoint), pMont);      // R = X^3
      PMA_mule(pR, pR, ECP_POINT_X(pPoint), pMont);

      /* case Z != 1 */
      if( !ECP_POINT_AFFINE(pPoint) ) {
         IppsBigNumState* pZ4 = cpBigNumListGet(&pList);
         IppsBigNumState* pZ6 = cpBigNumListGet(&pList);
         PMA_sqre(pT,  ECP_POINT_Z(pPoint), pMont);  // Z^2
         PMA_sqre(pZ4, pT,                pMont);  // Z^4
         PMA_mule(pZ6, pZ4, pT,           pMont);  // Z^6

         PMA_mule(pT, pZ4, ECP_POINT_X(pPoint), pMont); // T = X*Z^4
         if( ECP_AMI3(pECC) ) {
            IppsBigNumState* pU = cpBigNumListGet(&pList);
               PMA_add(pU, pT, pT, pModulo);             // R = X^3 +a*X*Z^4
               PMA_add(pU, pU, pT, pModulo);
               PMA_sub(pR, pR, pU, pModulo);
         }
         else {
            PMA_mule(pT, pT, ECP_AENC(pECC), pMont);  // R = X^3 +a*X*Z^4
            PMA_add(pR, pR, pT, pModulo);
         }
           PMA_mule(pT, pZ6, ECP_BENC(pECC), pMont);    // R = X^3 +a*X*Z^4 + b*Z^6
           PMA_add(pR, pR, pT, pModulo);

      }
      /* case Z == 1 */
      else {
         if( ECP_AMI3(pECC) ) {
               PMA_add(pT, ECP_POINT_X(pPoint), ECP_POINT_X(pPoint), pModulo); // R = X^3 +a*X
               PMA_add(pT, pT, ECP_POINT_X(pPoint), pModulo);
               PMA_sub(pR, pR, pT, pModulo);
         }
         else {
               PMA_mule(pT, ECP_POINT_X(pPoint), ECP_AENC(pECC), pMont);  // R = X^3 +a*X
               PMA_add(pR, pR, pT, pModulo);
         }
         PMA_add(pR, pR, ECP_BENC(pECC), pModulo);                   // R = X^3 +a*X + b
      }
      PMA_sqre(pT, ECP_POINT_Y(pPoint), pMont);  // T = Y^2
      return 0==cpBN_cmp(pR, pT);
   }
}

/*
// ECCP_ComparePoint
//
// Compare two points:
//    returns 0 => pP==pQ (maybe both pP and pQ are at Infinity)
//    returns 1 => pP!=pQ
//
// Note
// In general we check:
//    P_X*Q_Z^2 ~ Q_X*P_Z^2
//    P_Y*Q_Z^3 ~ Q_Y*P_Z^3
*/
int ECCP_ComparePoint(const IppsECCPPointState* pP,
                      const IppsECCPPointState* pQ,
                      const IppsECCPState* pECC,
                      BigNumNode* pList)
{
   /* P or/and Q at Infinity */
   if( ECCP_IsPointAtInfinity(pP) )
      return ECCP_IsPointAtInfinity(pQ)? 0:1;
   if( ECCP_IsPointAtInfinity(pQ) )
      return ECCP_IsPointAtInfinity(pP)? 0:1;

   /* (P_Z==1) && (Q_Z==1) */
    if( ECP_POINT_AFFINE(pP) && ECP_POINT_AFFINE(pQ) )
      return ((0==cpBN_cmp(ECP_POINT_X(pP),ECP_POINT_X(pQ))) && (0==cpBN_cmp(ECP_POINT_Y(pP),ECP_POINT_Y(pQ))))? 0:1;

   {
      IppsMontState* pMont = ECP_PMONT(pECC);

      IppsBigNumState* pPtmp = cpBigNumListGet(&pList);
      IppsBigNumState* pQtmp = cpBigNumListGet(&pList);
      IppsBigNumState* pPZ   = cpBigNumListGet(&pList);
      IppsBigNumState* pQZ   = cpBigNumListGet(&pList);

      /* P_X*Q_Z^2 ~ Q_X*P_Z^2 */
      if( !ECP_POINT_AFFINE(pQ) ) {
         PMA_sqre(pQZ, ECP_POINT_Z(pQ), pMont);      /* Ptmp = P_X*Q_Z^2 */
         PMA_mule(pPtmp, ECP_POINT_X(pP), pQZ, pMont);
      }
      else {
         PMA_set(pPtmp, ECP_POINT_X(pP));
      }
      if( !ECP_POINT_AFFINE(pP) ) {
         PMA_sqre(pPZ, ECP_POINT_Z(pP), pMont);      /* Qtmp = Q_X*P_Z^2 */
         PMA_mule(pQtmp, ECP_POINT_X(pQ), pPZ, pMont);
      }
      else {
         PMA_set(pQtmp, ECP_POINT_X(pQ));
      }
      if ( cpBN_cmp(pPtmp, pQtmp) )
         return 1;   /* points are different: (P_X*Q_Z^2) != (Q_X*P_Z^2) */

      /* P_Y*Q_Z^3 ~ Q_Y*P_Z^3 */
      if( !ECP_POINT_AFFINE(pQ) ) {
         PMA_mule(pQZ, pQZ, ECP_POINT_Z(pQ), pMont); /* Ptmp = P_Y*Q_Z^3 */
         PMA_mule(pPtmp, ECP_POINT_Y(pP), pQZ, pMont);
      }
      else {
         PMA_set(pPtmp, ECP_POINT_Y(pP));
      }
      if( !ECP_POINT_AFFINE(pP) ) {
         PMA_mule(pPZ, pPZ, ECP_POINT_Z(pP), pMont); /* Qtmp = Q_Y*P_Z^3 */
         PMA_mule(pQtmp, ECP_POINT_Y(pQ), pPZ, pMont);
      }
      else {
         PMA_set(pQtmp, ECP_POINT_Y(pQ));
      }
      return cpBN_cmp(pPtmp, pQtmp)? 1:0;
   }
}

/*
// ECCP_NegPoint
//
// Negative point
*/
void ECCP_NegPoint(const IppsECCPPointState* pP,
                   IppsECCPPointState* pR,
                   const IppsECCPState* pECC)
{
   /* test point at Infinity */
   if( ECCP_IsPointAtInfinity(pP) )
      ECCP_SetPointToInfinity(pR);

   else {
      IppsBigNumState* pModulo = ECP_PRIME(pECC);

      if( pP!=pR ) {
         PMA_set(ECP_POINT_X(pR), ECP_POINT_X(pP));
         PMA_set(ECP_POINT_Z(pR), ECP_POINT_Z(pP));
      }
      PMA_sub(ECP_POINT_Y(pR), pModulo, ECP_POINT_Y(pP), pModulo);
      ECP_POINT_AFFINE(pR) = ECP_POINT_AFFINE(pP);
   }
}

/*
// ECCP_DblPoint
//
// Double point
*/
void ECCP_DblPoint(const IppsECCPPointState* pP,
                   IppsECCPPointState* pR,
                   const IppsECCPState* pECC,
                   BigNumNode* pList)
{
   /* P at infinity */
   if( ECCP_IsPointAtInfinity(pP) )
      ECCP_SetPointToInfinity(pR);

   else {
      IppsMontState* pMont = ECP_PMONT(pECC);

      IppsBigNumState* bnV = cpBigNumListGet(&pList);
      IppsBigNumState* bnU = cpBigNumListGet(&pList);
      IppsBigNumState* bnM = cpBigNumListGet(&pList);
      IppsBigNumState* bnS = cpBigNumListGet(&pList);
      IppsBigNumState* bnT = cpBigNumListGet(&pList);
      IppsBigNumState* pModulo = ECP_PRIME(pECC);

      /* M = 3*X^2 + A*Z^4 */
       if( ECP_POINT_AFFINE(pP) ) {
           PMA_sqre(bnU, ECP_POINT_X(pP), pMont);
           PMA_add(bnM, bnU, bnU, pModulo);
           PMA_add(bnM, bnM, bnU, pModulo);
           PMA_add(bnM, bnM, ECP_AENC(pECC), pModulo);
        }
       else if( ECP_AMI3(pECC) ) {
           PMA_sqre(bnU, ECP_POINT_Z(pP), pMont);
           PMA_add(bnS, ECP_POINT_X(pP), bnU, pModulo);
           PMA_sub(bnT, ECP_POINT_X(pP), bnU, pModulo);
           PMA_mule(bnM, bnS, bnT, pMont);
           PMA_add(bnU, bnM, bnM, pModulo);
           PMA_add(bnM, bnU, bnM, pModulo);
        }
       else {
           PMA_sqre(bnU, ECP_POINT_X(pP), pMont);
           PMA_add(bnM, bnU, bnU, pModulo);
           PMA_add(bnM, bnM, bnU, pModulo);
           PMA_sqre(bnU, ECP_POINT_Z(pP), pMont);
           PMA_sqre(bnU, bnU, pMont);
           PMA_mule(bnU, bnU, ECP_AENC(pECC), pMont);
           PMA_add(bnM, bnM, bnU, pModulo);
        }

      PMA_add(bnV, ECP_POINT_Y(pP), ECP_POINT_Y(pP), pModulo);

      /* R_Z = 2*Y*Z */
      if( ECP_POINT_AFFINE(pP) ) {
         PMA_set(ECP_POINT_Z(pR), bnV);
      }
      else {
         PMA_mule(ECP_POINT_Z(pR), bnV, ECP_POINT_Z(pP), pMont);
      }

      /* S = 4*X*Y^2 */
      PMA_sqre(bnT, bnV, pMont);
      PMA_mule(bnS, bnT, ECP_POINT_X(pP), pMont);

      /* R_X = M^2 - 2*S */
      PMA_sqre(bnU, bnM, pMont);
      PMA_sub(bnU, bnU, bnS, pModulo);
      PMA_sub(ECP_POINT_X(pR), bnU, bnS, pModulo);

      /* T = 8*Y^4 */
      PMA_mule(bnV, bnV, ECP_POINT_Y(pP), pMont);
      PMA_mule(bnT, bnT, bnV, pMont);

      /* R_Y = M*(S - R_X) - T */
      PMA_sub(bnS, bnS, ECP_POINT_X(pR), pModulo);
      PMA_mule(bnS, bnS, bnM, pMont);
      PMA_sub(ECP_POINT_Y(pR), bnS, bnT, pModulo);

      ECP_POINT_AFFINE(pR) = 0;
   }
}

/*
// ECCP_AddPoint
//
// Add points
*/
void ECCP_AddPoint(const IppsECCPPointState* pP,
                   const IppsECCPPointState* pQ,
                   IppsECCPPointState* pR,
                   const IppsECCPState* pECC,
                   BigNumNode* pList)
{
   /* prevent operation with point at Infinity */
   if( ECCP_IsPointAtInfinity(pP) ) {
      ECCP_CopyPoint(pQ, pR);
      return;
   }
   if( ECCP_IsPointAtInfinity(pQ) ) {
      ECCP_CopyPoint(pP, pR);
      return;
   }

   /*
   // addition
   */
   {
      IppsMontState* pMont = ECP_PMONT(pECC);

      IppsBigNumState* bnU0 = cpBigNumListGet(&pList);
      IppsBigNumState* bnS0 = cpBigNumListGet(&pList);
      IppsBigNumState* bnU1 = cpBigNumListGet(&pList);
      IppsBigNumState* bnS1 = cpBigNumListGet(&pList);
      IppsBigNumState* bnW  = cpBigNumListGet(&pList);
      IppsBigNumState* bnR  = cpBigNumListGet(&pList);
      IppsBigNumState *bnT  = bnU0;
      IppsBigNumState *bnM  = bnS0;
      IppsBigNumState* pModulo = ECP_PRIME(pECC);

      /* U0 = P_X * Q_Z^2 */
      /* S0 = P_Y * Q_Z^3 */
      if( ECP_POINT_AFFINE(pQ) ) {
         PMA_set(bnU0, ECP_POINT_X(pP));
         PMA_set(bnS0, ECP_POINT_Y(pP));
      }
      else {
         PMA_sqre(bnW, ECP_POINT_Z(pQ),      pMont);
         PMA_mule(bnU0,ECP_POINT_X(pP), bnW, pMont);
         PMA_mule(bnW, ECP_POINT_Z(pQ), bnW, pMont);
         PMA_mule(bnS0,ECP_POINT_Y(pP), bnW, pMont);
      }

      /* U1 = Q_X * P_Z^2 */
      /* S1 = Q_Y * P_Z^3 */
      if( ECP_POINT_AFFINE(pP) ) {
         PMA_set(bnU1, ECP_POINT_X(pQ));
         PMA_set(bnS1, ECP_POINT_Y(pQ));
      }
      else {
         PMA_sqre(bnW, ECP_POINT_Z(pP),      pMont);
         PMA_mule(bnU1,ECP_POINT_X(pQ), bnW, pMont);
         PMA_mule(bnW, ECP_POINT_Z(pP), bnW, pMont);
         PMA_mule(bnS1,ECP_POINT_Y(pQ), bnW, pMont);
      }

      /* W = U0-U1 */
      /* R = S0-S1 */
      PMA_sub(bnW, bnU0, bnU1, pModulo);
      PMA_sub(bnR, bnS0, bnS1, pModulo);

      if( IsZero_BN(bnW) ) {
         if( IsZero_BN(bnR) ) {
            ECCP_DblPoint(pP, pR, pECC, pList);
            return;
         }
         else {
            ECCP_SetPointToInfinity(pR);
            return;
         }
      }

      /* T = U0+U1 */
      /* M = S0+S1 */
      PMA_add(bnT, bnU0, bnU1, pModulo);
      PMA_add(bnM, bnS0, bnS1, pModulo);

      /* R_Z = P_Z * Q_Z * W */
      if( ECP_POINT_AFFINE(pQ) && ECP_POINT_AFFINE(pP) ) {
         PMA_set(ECP_POINT_Z(pR), bnW);
      }
      else {
         if( ECP_POINT_AFFINE(pQ) ) {
            PMA_set(bnU1, ECP_POINT_Z(pP));
         }
         else if( ECP_POINT_AFFINE(pP) ) {
            PMA_set(bnU1, ECP_POINT_Z(pQ));
         }
         else {
            PMA_mule(bnU1, ECP_POINT_Z(pP), ECP_POINT_Z(pQ), pMont);
         }
         PMA_mule(ECP_POINT_Z(pR), bnU1, bnW, pMont);
      }

      PMA_sqre(bnU1, bnW, pMont);         /* U1 = W^2     */
      PMA_mule(bnS1, bnT, bnU1, pMont);   /* S1 = T * W^2 */

      /* R_X = R^2 - T * W^2 */
      PMA_sqre(ECP_POINT_X(pR), bnR, pMont);
      PMA_sub(ECP_POINT_X(pR), ECP_POINT_X(pR), bnS1, pModulo);

      /* V = T * W^2 - 2 * R_X  (S1) */
      PMA_sub(bnS1, bnS1, ECP_POINT_X(pR), pModulo);
      PMA_sub(bnS1, bnS1, ECP_POINT_X(pR), pModulo);

      /* R_Y = (V * R - M * W^3) /2 */
      PMA_mule(ECP_POINT_Y(pR), bnS1, bnR, pMont);
      PMA_mule(bnU1, bnU1, bnW, pMont);
      PMA_mule(bnU1, bnU1, bnM, pMont);
      PMA_sub(bnU1, ECP_POINT_Y(pR), bnU1, pModulo);
      PMA_div2(ECP_POINT_Y(pR), bnU1, pModulo);

      ECP_POINT_AFFINE(pR) = 0;
   }
}

/*
// ECCP_MulPoint
//
// Multiply point by scalar
*/
void ECCP_MulPoint(const IppsECCPPointState* pP,
                   const IppsBigNumState* bnN,
                   IppsECCPPointState* pR,
                   const IppsECCPState* pECC,
                   BigNumNode* pList)
{
   /* test zero scalar or input point at Infinity */
   if( IsZero_BN(bnN) || ECCP_IsPointAtInfinity(pP) ) {
      ECCP_SetPointToInfinity(pR);
      return;
   }

   /*
   // scalar multiplication
   */
   else {
      Ipp8u* pScratchAligned = ECP_SCCMBUFF(pECC);

      BNU_CHUNK_T* pN = BN_NUMBER(bnN);
      cpSize nsN = BN_SIZE(bnN);
      /* scalar bitsize */
      int scalarBitSize = BITSIZE_BNU(pN, nsN);
      /* optimal size of window */
      int w = cpECCP_OptimalWinSize(scalarBitSize);
      /* number of table entries */
      int nPrecomputed = 1<<w;

      /* allocate temporary scalar */
      IppsBigNumState* bnTN = cpBigNumListGet(&pList);
      BNU_CHUNK_T* pTN = BN_NUMBER(bnTN);

      int coordSize = BITS_BNU_CHUNK(ECP_GFEBITS(pECC));
      IppsECCPPointState T;
      ECP_POINT_X(&T) = cpBigNumListGet(&pList);
      ECP_POINT_Y(&T) = cpBigNumListGet(&pList);
      ECP_POINT_Z(&T) = cpBigNumListGet(&pList);
      ECCP_SetPointToInfinity(&T);

      /* init result */
      ECCP_CopyPoint(pP, pR);
      if( ippBigNumNEG == BN_SIGN(bnN) )
         ECCP_NegPoint(pR, pR, pECC);

      /* pre-compute auxiliary table t[] = {(2^w)*P, 1*P, 2*P, ..., (2^(w-1))*P} */
      {
         int n;
         for(n=1; n<nPrecomputed; n++) {
            ECCP_AddPoint(pR, &T, &T, pECC, pList);
            cpECCP_ScramblePut(pScratchAligned+n, nPrecomputed, &T, coordSize);
         }
         ECCP_AddPoint(pR, &T, &T, pECC, pList);
         cpECCP_ScramblePut(pScratchAligned, nPrecomputed, &T, coordSize);
      }

      /* copy scalar */
      cpCpy_BNU(pTN, pN, nsN);
      /* and convert it presentaion to avoid usage of O point */
      scalarBitSize = cpECCP_ConvertRepresentation(pTN, scalarBitSize, w);

      /* prepare temporary scalar for processing */
      pTN[BITS_BNU_CHUNK(scalarBitSize)] = 0;
      scalarBitSize = ((scalarBitSize+w-1)/w)*w;

      /*
      // scalar multiplication
      */
      {
         Ipp32u dmask = nPrecomputed-1;

         /* position (bit number) of the leftmost window */
         int wPosition = scalarBitSize-w;

         /* extract leftmost window value */
         Ipp32u eChunk = *((Ipp32u*)((Ipp16u*)pTN + wPosition/BITSIZE(Ipp16u)));
         int shift = wPosition & 0xF;
         Ipp32u windowVal = (eChunk>>shift) & dmask;

         /* initialize result (ECP_FINITE_POINT|ECP_PROJECTIVE) */
         cpECCP_ScrambleGet(pR, coordSize, pScratchAligned+windowVal, nPrecomputed);
         ECP_POINT_AFFINE(pR) = 0;

         /* initialize temporary T (ECP_PROJECTIVE) */
         ECP_POINT_AFFINE(&T) = 0;

         for(wPosition-=w; wPosition>=0; wPosition-=w) {
            /* w times doubling */
            int k;
            for(k=0; k<w; k++)
               ECCP_DblPoint(pR, pR, pECC, pList);

            /* extract next window value */
            eChunk = *((Ipp32u*)((Ipp16u*)pTN + wPosition/BITSIZE(Ipp16u)));
            shift = wPosition & 0xF;
            windowVal = (eChunk>>shift) & dmask;

            /* extract value from the pre-computed table */
            cpECCP_ScrambleGet(&T, coordSize, pScratchAligned+windowVal, nPrecomputed);

            /* and add it */
            ECCP_AddPoint(pR, &T, pR, pECC, pList);
         }
      }
   }
}


void ECCP_MulBasePoint(const IppsBigNumState* pK,
                    IppsECCPPointState* pR,
                    const IppsECCPState* pECC,
                    BigNumNode* pList)
{
   ECCP_MulPoint(ECP_GENC(pECC), pK, pR, pECC, pList);
}

/*
// ECCP_ProdPoint
//
// Point product
*/
void ECCP_ProdPoint(const IppsECCPPointState* pP,
                    const IppsBigNumState*    bnPscalar,
                    const IppsECCPPointState* pQ,
                    const IppsBigNumState*    bnQscalar,
                    IppsECCPPointState* pR,
                    const IppsECCPState* pECC,
                    BigNumNode* pList)
{
   IppsECCPPointState T;
   IppsECCPPointState U;

   ECP_POINT_X(&T) = cpBigNumListGet(&pList);
   ECP_POINT_Y(&T) = cpBigNumListGet(&pList);
   ECP_POINT_Z(&T) = cpBigNumListGet(&pList);

   ECP_POINT_X(&U) = cpBigNumListGet(&pList);
   ECP_POINT_Y(&U) = cpBigNumListGet(&pList);
   ECP_POINT_Z(&U) = cpBigNumListGet(&pList);

   ECCP_MulPoint(pP, bnPscalar, &T, (IppsECCPState*)pECC, pList);
   ECCP_MulPoint(pQ, bnQscalar, &U, (IppsECCPState*)pECC, pList);
   ECCP_AddPoint(&T, &U, pR, pECC, pList);
}
