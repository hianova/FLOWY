import Init.Data.BitVec

open BitVec

/-! ## Pillar 1: BitVec 64 Vulnerability Healing Witness -/
def max_i64 : BitVec 64 := 0x7FFFFFFFFFFFFFFF#64
def naive_add (a b : BitVec 64) : BitVec 64 := a + b
def dietz_midpoint (a b : BitVec 64) : BitVec 64 := (a &&& b) + ((a ^^^ b) >>> 1)

-- Naive addition overflows for max_i64 + max_i64
theorem naive_overflow_witness : naive_add max_i64 max_i64 = 0xFFFFFFFFFFFFFFFE#64 := by rfl

-- Dietz midpoint heals overflow witness
theorem vuln_healed_witness : dietz_midpoint max_i64 max_i64 = max_i64 := by rfl

-- Boundary Soundness
theorem boundary_sound (x : BitVec 64) : (x &&& x) = x := by simp

-- Interior Equivalence under identity
theorem interior_equiv (a b : BitVec 64) (h : a = b) : dietz_midpoint a b = a := by
  subst h
  simp [dietz_midpoint]

/-! ## Pillar 2: Perspective Cross-Section & Parallax Precision (by decide) -/
def F : Nat := 3326
abbrev sliceSafe (X d Z : Nat) : Prop := 2 * F * X * d < Z * (Z + d)

theorem ship_at_1km_ok      : sliceSafe 10000 10000 1000000 := by decide
theorem ship_at_500m_breaks : ¬ sliceSafe 10000 10000 500000 := by decide
theorem thin_at_500m_ok     : sliceSafe 10000 2000 500000 := by decide

/-! ## Pillar 3: Integer-Lattice Conservation & Hysteresis Invariants (by omega) -/
theorem tax_split_conservation (total fee : Int) : fee + (total - fee) = total := by omega

theorem anti_tunneling_swept_invariant (a b L : Int) (h : L >= (b - a) + 2) : L > (b - a) := by omega

theorem hysteresis_deadband_well_ordered (t d : Int) (hd : d > 0) : t - d < t + d := by omega
