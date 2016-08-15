#include "../include/penv.h"
#include "../include/pbound.h"
#include "../include/pfloat.h"
#include <stdio.h>

int addsub_index(long long lhs_lattice, long long rhs_lattice){
  //matrix points are generated by shifting away the ulp status.
  int lpoint1 = (lhs_lattice >> 1);
  int lpoint2 = (rhs_lattice >> 1);
  //shift the left value index by the number of lattice bits.
  return (lpoint1 << (PENV->latticebits - 1)) + lpoint2;
}

static void exact_arithmetic_addition_crossed(PBound *dest, PFloat lhs, PFloat rhs){

  TRACK("entering exact_arithmetic_addition_crossed...");

  int res_epoch = pf_epoch(lhs);
  unsigned long long res_lattice;
  unsigned long long lhs_lattice = pf_lattice(lhs);
  unsigned long long rhs_lattice = pf_lattice(rhs);

  if ((res_epoch == 0) && (pf_epoch(rhs) == 0)) {
    res_lattice = (PENV->tables[__ADD_CROSSED_TABLE])[addsub_index(lhs_lattice, rhs_lattice)];
    res_epoch = (res_lattice < lhs_lattice) ? 1 : 0;
  } else {
    //nothing, for now.
    return;
  }

  set_single(dest, pf_synth(is_pf_negative(lhs), false, res_epoch, res_lattice));
}

static void exact_arithmetic_addition_inverted(PBound *dest, PFloat lhs, PFloat rhs){

  TRACK("entering exact_arithmetic_addition_inverted...");

  int res_epoch = pf_epoch(lhs);
  unsigned long long res_lattice;
  unsigned long long lhs_lattice = pf_lattice(lhs);
  unsigned long long rhs_lattice = pf_lattice(rhs);
  bool res_sign = is_pf_negative(lhs);
  bool res_inverted = true;

  if (res_epoch == pf_epoch(rhs)){
    res_lattice = (PENV->tables[__ADD_INVERTED_TABLE])[addsub_index(lhs_lattice, rhs_lattice)];
    res_epoch -= (res_lattice > lhs_lattice) ? 1 : 0;
  } else {
    //nothing, for now.
    return;
  }

  if (res_epoch < 0) {
    res_inverted = false;
    res_epoch = (-res_epoch) - 1;
  }

  if (res_inverted) {
    set_single(dest, pf_synth(res_sign, true, res_epoch, res_lattice));
  } else if (__is_lattice_ulp(res_lattice)) {
    PFloat _l = upper_ulp(pf_synth(res_sign, false, res_epoch, invert(res_lattice + 1)));
    PFloat _u = lower_ulp(pf_synth(res_sign, false, res_epoch, invert(res_lattice - 1)));
    set_bound(dest, _l, _u);
    collapseifsingle(dest);
  } else {
    set_single(dest, pf_synth(res_sign, false, res_epoch, invert(res_lattice)));
  }
}

static void exact_arithmetic_addition_uninverted(PBound *dest, PFloat lhs, PFloat rhs){

  TRACK("entering exact_arithmetic_uninverted...");

  int res_epoch = pf_epoch(lhs);
  unsigned long long res_lattice;
  unsigned long long lhs_lattice = pf_lattice(lhs);
  unsigned long long rhs_lattice = pf_lattice(rhs);

  if (res_epoch == pf_epoch(rhs)){
    res_lattice = (PENV->tables[__ADD_TABLE])[addsub_index(lhs_lattice, rhs_lattice)];
    res_epoch += (res_lattice < lhs_lattice) ? 1 : 0;
  } else {
    //nothing, for now.
    return;
  }

  set_single(dest, pf_synth(is_pf_negative(lhs), false, res_epoch, res_lattice));
}

static void exact_arithmetic_addition(PBound *dest, PFloat lhs, PFloat rhs){
  TRACK("entering exact_arithmetic_addition...");

  //swap the order of the two terms to make sure that the outer float appears
  //first.
  bool orderswap = is_pf_negative(lhs) ^ (__s(lhs) < __s(rhs));

  PFloat outer = orderswap ? rhs : lhs;
  PFloat inner = orderswap ? lhs : rhs;

  if (is_pf_inverted(outer) ^ is_pf_inverted(inner)) {
    exact_arithmetic_addition_crossed(dest, outer, inner);
  } else if (is_pf_inverted(outer)) {
    exact_arithmetic_addition_inverted(dest, outer, inner);
  } else {
    exact_arithmetic_addition_uninverted(dest, outer, inner);
  }
}

static void pf_exact_add(PBound *dest, PFloat lhs, PFloat rhs){
  TRACK("entering pf_exact_add...");

  //redo the checks on this in case we've been passed from inexact_add.
  if (is_pf_inf(lhs)) {set_inf(dest); return;}
  if (is_pf_inf(rhs)) {set_inf(dest); return;}
  if (is_pf_zero(lhs)) {set_single(dest, rhs); return;}
  if (is_pf_zero(rhs)) {set_single(dest, lhs); return;}

  if (is_pf_negative(lhs) ^ is_pf_negative(rhs)){
    exact_arithmetic_subtraction(dest, lhs, pf_additiveinverse(rhs));
  } else {
    exact_arithmetic_addition(dest, lhs, rhs);
  }
}

static void pf_inexact_add(PBound *dest, PFloat lhs, PFloat rhs){
  TRACK("entering pf_inexact_add...");

  PBound temp = __EMPTYBOUND;
  pf_exact_add(&temp, glb(lhs), glb(rhs));
  PFloat _l = upper_ulp(temp.lower);
  pf_exact_add(&temp, lub(lhs), lub(rhs));
  PFloat _u = (temp.state == SINGLETON) ? lower_ulp(temp.lower) : lower_ulp(temp.upper);
  set_bound(dest, _l, _u);
  collapseifsingle(dest);
}

static void pf_add(PBound *dest, PFloat lhs, PFloat rhs){
  TRACK("entering pf_add...");

  if (is_pf_inf(lhs)) {set_inf(dest); return;}
  if (is_pf_inf(rhs)) {set_inf(dest); return;}
  if (is_pf_zero(lhs)) {set_single(dest, rhs); return;}
  if (is_pf_zero(rhs)) {set_single(dest, lhs); return;}

  if (is_pf_exact(lhs) && is_pf_exact(rhs)) {
    pf_exact_add(dest, lhs, rhs);
  } else {
    pf_inexact_add(dest, lhs, rhs);
  }
}

void add(PBound *dest, const PBound *lhs, const PBound *rhs){
  TRACK("entering add...");

  /*first handle all the corner cases*/
  if (isempty(lhs) || isempty(rhs)) {set_empty(dest); return;}
  if (isallpreals(lhs) || isallpreals(rhs)) {set_allreals(dest); return;}

  if (issingle(lhs) && issingle(rhs)) {pf_add(dest, lhs->lower, rhs->lower); return;}

  //assign proxy values because "single" doesn't have a meaningful upper value.
  PFloat lhs_upper_proxy = issingle(lhs) ? lhs->lower : lhs->upper;
  PFloat rhs_upper_proxy = issingle(rhs) ? rhs->lower : rhs->upper;

  //NB: consider going directly to "single_add" with a double single check.
  //go ahead and do both sides of the bound calculation.  Use a temporary variable.
  PBound temp = __EMPTYBOUND;
  dest->state = STDBOUND;
  pf_add(&temp, lhs->lower, rhs->lower);
  dest->lower = temp.lower;
  pf_add(&temp, lhs_upper_proxy, rhs_upper_proxy);
  dest->upper = issingle(&temp) ? temp.lower : temp.upper;

  if (roundsinf(lhs) || roundsinf(rhs)){
    // let's make sure our result still rounds inf, this is a property which is
    // invariant under addition.  Losing this property suggests that the answer
    // should be recast as "allreals."  While we're at it, check to see if the
    // answer ends now "touch", which makes them "allreals".
    if (__s(dest->lower) <= __s(dest->upper)) {
      set_allreals(dest);
    } else if (next(dest->upper) == dest->lower) {
      set_allreals(dest);
    }
  }

  collapseifsingle(dest);
}

