Inductive MtxN : Type :=
  | Mtx1
  | Mtx2
  | Mtx3.

Inductive Actor : Type :=
  | Actor1
  | Actor2.

Inductive Mtx : Type :=
  | MtxL (owner : Actor)
  | MtxU.

Inductive State : Type :=
  | state (mtx_id1 mtx_id2 : MtxN) (m1 m2 m3 : Mtx).

Definition state_get_mtx (s : State) (n : MtxN) : Mtx :=
  match n, s with
  | Mtx1, state _ _ m1 _ _ => m1
  | Mtx2, state _ _ _ m2 _ => m2
  | Mtx3, state _ _ _ _ m3 => m3
  end.

Definition state_set_mtx (s : State) (n : MtxN) (m : Mtx) : State :=
  match s with
  | state mtx_id1 mtx_id2 m1 m2 m3 =>
      match n with
      | Mtx1 => state mtx_id1 mtx_id2 m m2 m3
      | Mtx2 => state mtx_id1 mtx_id2 m1 m m3
      | Mtx3 => state mtx_id1 mtx_id2 m1 m2 m
      end
  end.

Definition state_set_mtx_id (s : State) (actor : Actor) (n : MtxN) : State :=
  match s with
  | state mtx_id1 mtx_id2 m1 m2 m3 =>
      match actor with
      | Actor1 => state n mtx_id2 m1 m2 m3
      | Actor2 => state mtx_id1 n m1 m2 m3
      end
  end.

Inductive Owns : Actor -> MtxN -> State -> Prop :=
  | owns owner n s :
      state_get_mtx s n = MtxL owner ->
      Owns owner n s.

Inductive Free : MtxN -> State -> Prop :=
  | free n s :
      state_get_mtx s n = MtxU ->
      Free n s.

Inductive Unlocked : Actor -> MtxN -> State -> State -> Prop :=
  | unlocked n owner s :
      Owns owner n s ->
      Unlocked owner n s (state_set_mtx s n MtxU).

Inductive Locked : Actor -> MtxN -> State -> State -> Prop :=
  | locked n owner s :
      Free n s ->
      Locked owner n s (state_set_mtx s n (MtxL owner)).

Definition next_mtx (m : MtxN) : MtxN :=
  match m with
  | Mtx1 => Mtx2
  | Mtx2 => Mtx3
  | Mtx3 => Mtx1
  end.

Definition prev_mtx (m : MtxN) : MtxN :=
  match m with
  | Mtx1 => Mtx3
  | Mtx2 => Mtx1
  | Mtx3 => Mtx2
  end.

Lemma prev_mtx_inv_next_mtx : forall m m',
  m = next_mtx m' <-> prev_mtx m = m'.
Proof.
  intros m m'.
  destruct m, m'; firstorder; simpl in * |- *; discriminate H.
Qed.

Lemma next_prev_mtx_inv : forall m,
  m = next_mtx (prev_mtx m).
Proof.
  intros []; firstorder.
Qed.

Lemma prev_next_mtx_inv : forall m,
  m = prev_mtx (next_mtx m).
Proof.
  intros []; firstorder.
Qed.

Definition current_mtx_id (actor : Actor) (s : State) : MtxN :=
  match s with
  | state mtx_id1 mtx_id2 _ _ _ => match actor with
                                   | Actor1 => mtx_id1
                                   | Actor2 => mtx_id2
                                   end
  end.

Lemma current_mtx_id_elim : forall actor s n,
  current_mtx_id actor (state_set_mtx_id s actor n) = n.
Proof.
  intros [] [mtx_id1 mtx_id2 m1 m2 m3] []; simpl in * |- *; congruence.
Qed.

Lemma current_mtx_id_elim' : forall actor actor' s n,
  actor <> actor' ->
  current_mtx_id actor' (state_set_mtx_id s actor n) = current_mtx_id actor' s.
Proof.
  intros [] [] [mtx_id1 mtx_id2 m1 m2 m3] []; simpl in * |- *; congruence.
Qed.

Inductive ProgramCounter : Type :=
  | PCLock
  | PCPrint
  | PCUnlock.

Inductive Thread : Type :=
  | thread (pc : ProgramCounter) (s : State).

Inductive Executor : Type :=
  | executor (pc1 pc2 : ProgramCounter) (s : State).

Definition executor_state (e : Executor) : State :=
  match e with
  | executor _ _ s => s
  end.

Definition executor_pc (e : Executor) (actor : Actor) : ProgramCounter :=
  match actor, e with
  | Actor1, executor pc _ _ => pc
  | Actor2, executor _ pc _ => pc
  end.

Inductive ThreadStep : Actor -> Thread -> Thread -> Prop :=
  | thread_pc_lock actor : forall s s',
      current_mtx_id actor s = current_mtx_id actor s' ->
      Locked actor (next_mtx (current_mtx_id actor s)) s s' ->
      ThreadStep actor (thread PCLock s) (thread PCPrint s')
  | thread_pc_unlock actor : forall s s' s'',
      (* s' has the right mtx state, but a wrong mtx_id *)
      Unlocked actor (current_mtx_id actor s) s s' ->
      s'' = state_set_mtx_id s' actor (next_mtx (current_mtx_id actor s')) ->
      ThreadStep actor (thread PCUnlock s) (thread PCLock s'')
  | thread_pc_print actor : forall s s',
      s = s' ->
      ThreadStep actor (thread PCPrint s) (thread PCUnlock s').

Inductive ExecutorStep : Executor -> Executor -> Prop :=
  | exec_thread1 pc1 pc1' pc2 s s' :
      ThreadStep Actor1 (thread pc1 s) (thread pc1' s') ->
      ExecutorStep (executor pc1 pc2 s) (executor pc1' pc2 s')
  | exec_thread2 pc1 pc2 pc2' s s' :
      ThreadStep Actor2 (thread pc2 s) (thread pc2' s') ->
      ExecutorStep (executor pc1 pc2 s) (executor pc1 pc2' s').

Notation "e '~~>' e'" := (ExecutorStep e e') (at level 70, no associativity).

Definition initial_executor : Executor :=
  executor PCPrint PCLock
    (state Mtx1 Mtx3 (MtxL Actor1) (MtxL Actor1) (MtxL Actor2)).

Inductive ExecReachable (e : Executor) : Executor -> Prop :=
  | exec_refl : ExecReachable e e
  | exec_trans e' e'' (H : ExecReachable e e') :
      e' ~~> e'' ->
      ExecReachable e e''.

Notation "e '==>' e'" := (ExecReachable e e') (at level 70, no associativity).

Lemma state_get_set_mtx : forall n m s,
  state_get_mtx (state_set_mtx s n m) n = m.
Proof.
  intros [] m [mtx_id1 mtx_id2 m1 m2 m3]; reflexivity.
Qed.

Lemma locked_implies_owns : forall actor n s s',
  Locked actor n s s' -> Owns actor n s'.
Proof.
  intros actor n s s' HLocked.
  inversion HLocked. apply owns. apply state_get_set_mtx.
Qed.

Lemma free_xor_owned : forall actor n s,
  Free n s -> Owns actor n s -> False.
Proof.
  intros actor n s HFree HOwns.
  destruct HFree. destruct HOwns. rewrite -> H0 in H. discriminate H.
Qed.

Lemma unlocked_preserves_state : forall actor n m s s',
  n <> m ->
  Unlocked actor n s s' ->
  state_get_mtx s m = state_get_mtx s' m.
Proof.
  intros actor n m [mi1 mi2 m1 m2 m3] [mi1' mi2' m1' m2' m3'] Hneq HUnlocked.
  inversion HUnlocked as [n' actor' s].
  destruct n, m; firstorder.
Qed.

Lemma locked_preserves_state : forall actor n m s s',
  n <> m ->
  Locked actor n s s' ->
  state_get_mtx s m = state_get_mtx s' m.
Proof.
  intros actor n m s s' Hneq HLocked.
  inversion HLocked. subst.
  destruct n, m, s; firstorder.
Qed.

Lemma unlocked_preserves_ids : forall owner actor n s s',
  Unlocked owner n s s' ->
  current_mtx_id actor s = current_mtx_id actor s'.
Proof.
  intros owner actor n s s' HUnlocked.
  inversion HUnlocked. subst.
  destruct actor, s, n; firstorder.
Qed.

Lemma locked_preserves_ids : forall owner actor n s s',
  Locked owner n s s' ->
  current_mtx_id actor s = current_mtx_id actor s'.
Proof.
  intros owner actor n s s' HLocked.
  inversion HLocked. subst.
  destruct actor, s, n; firstorder.
Qed.

Lemma unlocked_preserves_owns : forall actor owner n m s s',
  n <> m ->
  Unlocked owner n s s' ->
  (Owns actor m s <-> Owns actor m s').
Proof.
  intros actor owner n m s s' Hneq HUnlocked.
  split; intros HOwns; apply owns.
  - rewrite <- unlocked_preserves_state with (n := n) (s := s) (actor := owner).
    + destruct HOwns. apply H.
    + apply Hneq.
    + apply HUnlocked.
  - rewrite -> unlocked_preserves_state
      with (n := n) (s := s) (actor := owner) (s' := s').
    + destruct HOwns. apply H.
    + apply Hneq.
    + apply HUnlocked.
Qed.

Lemma locked_preserves_owns : forall actor owner n m s s',
  n <> m ->
  Locked owner n s s' ->
  (Owns actor m s <-> Owns actor m s').
Proof.
  intros actor owner n m s s' Hneq HLocked.
  split; intros HOwns; apply owns.
  - rewrite <- locked_preserves_state with (n := n) (s := s) (actor := owner).
    + destruct HOwns. apply H.
    + apply Hneq.
    + apply HLocked.
  - rewrite -> locked_preserves_state
      with (n := n) (s := s) (actor := owner) (s' := s').
    + destruct HOwns. apply H.
    + apply Hneq.
    + apply HLocked.
Qed.

Lemma locked_preserves_free : forall owner n m s s',
  n <> m ->
  Free m s ->
  Locked owner n s s' ->
  Free m s'.
Proof.
  intros owner n m s s' Hneq HFree HLocked.
  apply free.
  rewrite <- locked_preserves_state with (actor := owner) (n := n) (s := s).
  - destruct HFree. apply H.
  - apply Hneq.
  - apply HLocked.
Qed.

Lemma owns_injective_actor : forall actor actor' n s,
  Owns actor n s ->
  Owns actor' n s ->
  actor = actor'.
Proof.
  intros actor actor' n s HOwns HOwns'.
  destruct HOwns.
  destruct HOwns'.
  rewrite -> H in H0.
  injection H0 as H'.
  apply H'.
Qed.

Lemma unlocked_retains_others_owns : forall actor actor' n m s s',
  actor <> actor' ->
  Owns actor n s ->
  Unlocked actor' m s s' ->
  Owns actor n s'.
Proof.
  intros actor actor' n m s s'.
  intros Hneq HOwns HUnlocked.
  inversion HUnlocked as [unlocked_n unlocked_owner unlocked_s HOwns'].
  subst unlocked_n unlocked_owner unlocked_s.
  inversion HOwns' as [? ? ? Heq']. subst.
  destruct n, m, actor, actor', s;
    simpl in Heq'; rewrite -> Heq' in * |- *; simpl;
    try contradiction; inversion HOwns as [? ? ? Heq]; subst; simpl in * |- *;
    try discriminate Heq; rewrite -> Heq in * |- *;
    apply owns; firstorder.
Qed.

Lemma locked_retains_others_owns : forall actor actor' n m s s',
  actor <> actor' ->
  Owns actor n s ->
  Locked actor' m s s' ->
  Owns actor n s'.
Proof.
  intros actor actor' n m s s'.
  intros Hneq HOwns HLocked.
  inversion HLocked as [? ? ? HFree]. subst.
  inversion HFree as [? ? Hmtx_u]. subst.
  inversion HOwns as [? ? ? Heq]. subst.
  destruct n, m, actor, actor', s;
    simpl in Hmtx_u;
    rewrite -> Hmtx_u in * |- *; simpl;
    try contradiction;
    inversion HFree as [? ? Heq']; subst; simpl in * |- *;
    try discriminate Heq;
    rewrite -> Heq in * |- *;
    apply owns; firstorder.
Qed.

Lemma unlocked_makes_free : forall n actor s s',
  Unlocked actor n s s' ->
  Free n s'.
Proof.
  intros n actor s s' HUnlocked.
  inversion HUnlocked
    as [unlocked_n unlocked_actor unlocked_s HOwns Eactor En Es Es'].
  apply free, state_get_set_mtx.
Qed.

Lemma next_mtx_distinct : forall n, n <> next_mtx n.
Proof.
  intros []; simpl; congruence.
Qed.

Definition pc_eqv (pc pc' : ProgramCounter) : bool :=
  match pc, pc' with
  | PCLock, PCLock => true
  | PCUnlock, PCUnlock => true
  | PCPrint, PCPrint => true
  | _, _ => false
  end.

Lemma pc_eqv_eq : forall pc pc', pc_eqv pc pc' = true <-> pc = pc'.
Proof.
  intros [] []; firstorder; simpl in H; discriminate H.
Qed.

Definition mtx_id_eqv (n n' : MtxN) : bool :=
  match n, n' with
  | Mtx1, Mtx1 => true
  | Mtx2, Mtx2 => true
  | Mtx3, Mtx3 => true
  | _, _ => false
  end.

Lemma mtx_id_eqv_eq : forall n n', mtx_id_eqv n n' = true <-> n = n'.
Proof.
  intros [] []; firstorder; discriminate H.
Qed.

Definition actor_eqv (actor actor' : Actor) : bool :=
  match actor, actor' with
  | Actor1, Actor1 => true
  | Actor2, Actor2 => true
  | _, _ => false
  end.

Lemma actor_eqv_eq : forall actor actor',
  actor_eqv actor actor' = true <-> actor = actor'.
Proof.
  intros [] []; firstorder; discriminate H.
Qed.

Definition mtx_eqv (m m' : Mtx) : bool :=
  match m, m' with
  | MtxL owner, MtxL owner' => actor_eqv owner owner'
  | MtxU, MtxU => true
  | _, _ => false
  end.

Lemma mtx_eqv_eq : forall m m', mtx_eqv m m' = true <-> m = m'.
Proof.
  intros [] []; firstorder; try discriminate H;
    destruct owner; destruct owner0; firstorder; discriminate H.
Qed.

Definition state_eqv (s s' : State) : bool :=
  match s, s' with
  | state mtx_id1 mtx_id2 m1 m2 m3, state mtx_id1' mtx_id2' m1' m2' m3' =>
      mtx_id_eqv mtx_id1 mtx_id1' &&
      mtx_id_eqv mtx_id2 mtx_id2' &&
      mtx_eqv m1 m1' &&
      mtx_eqv m2 m2' &&
      mtx_eqv m3 m3'
  end.

Lemma state_eqv_eq : forall s s', state_eqv s s' = true <-> s = s'.
Proof.
  intros [] []; firstorder; try discriminate H;
    repeat (apply andb_prop in H; destruct H).

  - apply mtx_eqv_eq in H0, H1, H2.
    apply mtx_id_eqv_eq in H, H3.
    subst. reflexivity.
  - injection H. intros. simpl.
    repeat (apply andb_true_intro; split);
      try apply mtx_eqv_eq;
      try apply mtx_id_eqv_eq;
      assumption.
Qed.

Definition exec_eqv (e e' : Executor) : bool :=
  match e, e' with executor pc1 pc2 s, executor pc1' pc2' s' =>
    pc_eqv pc1 pc1' && pc_eqv pc2 pc2' && state_eqv s s'
  end.

Lemma exec_eqv_eq : forall e e', exec_eqv e e' = true <-> e = e'.
Proof.
  intros [] []; firstorder; try discriminate H.
  - repeat (apply andb_prop in H; destruct H).
    apply pc_eqv_eq in H, H1.
    apply state_eqv_eq in H0.
    subst. reflexivity.
  - injection H. intros.
    repeat (apply andb_true_intro; split);
      try apply pc_eqv_eq;
      try apply state_eqv_eq;
      assumption.
Qed.

Definition other_actor (actor : Actor) : Actor :=
  match actor with
  | Actor1 => Actor2
  | Actor2 => Actor1
  end.

Inductive PossibleExecutor : Executor -> Prop :=
  | possible_exec_xl (e : Executor) (actor : Actor) :
      let s := executor_state e
      in let actor' := other_actor actor
      in let mtx_id := current_mtx_id actor s
      in let mtx_id' := current_mtx_id actor' s
      in (executor_pc e actor = PCPrint \/ executor_pc e actor = PCUnlock) ->
         executor_pc e actor' = PCLock ->
         mtx_id' = prev_mtx mtx_id ->
         Owns actor mtx_id s ->
         Owns actor (next_mtx mtx_id) s ->
         Owns actor' mtx_id' s ->
         PossibleExecutor e

  | possible_exec_ll (e : Executor) (actor : Actor) :
      let s := executor_state e
      in let actor' := other_actor actor
      in let mtx_id := current_mtx_id actor s
      in let mtx_id' := current_mtx_id actor' s
      in executor_pc e actor = PCLock ->
         executor_pc e actor' = PCLock ->
         mtx_id' = next_mtx mtx_id ->
         Free (prev_mtx mtx_id) s ->
         Owns actor mtx_id s ->
         Owns actor' mtx_id' s ->
         PossibleExecutor e.

Lemma double_next_mtx : forall n,
  next_mtx (next_mtx n) = prev_mtx n.
Proof.
  intros []; firstorder.
Qed.

Lemma double_prev_mtx : forall n,
  prev_mtx (prev_mtx n) = next_mtx n.
Proof.
  intros []; firstorder.
Qed.

Lemma state_set_mtx_id_keeps_mtx : forall actor n n' s s',
  s' = state_set_mtx_id s actor n ->
  state_get_mtx s n' = state_get_mtx s' n'.
Proof.
  intros [] [] [];
    intros [mtx_id1 mtx_id2 m1 m2 m3] [mtx_id1' mtx_id2' m1' m2' m3'] Heq;
    simpl in * |- *; congruence.
Qed.

Lemma state_set_mtx_keeps_mtx_id : forall actor n actor' s s',
  s' = state_set_mtx s actor n ->
  current_mtx_id actor' s = current_mtx_id actor' s'.
Proof.
  intros [] [] [];
    intros [mtx_id1 mtx_id2 m1 m2 m3] [mtx_id1' mtx_id2' m1' m2' m3'] Heq;
    simpl in * |- *; congruence.
Qed.

Lemma state_set_mtx_id_keeps_free : forall actor n s n' s',
  s' = state_set_mtx_id s actor n ->
  Free n' s <-> Free n' s'.
Proof.
  intros [] [] [mtx_id1 mtx_id2 m1 m2 m3];
    intros [] [mtx_id1' mtx_id2' m1' m2' m3'] Es'; rewrite -> Es';
    split; simpl;
    intros HFree; inversion HFree;
    subst; simpl in * |-; subst;
    apply free; reflexivity.
Qed.

Lemma state_set_mtx_id_keeps_owns : forall actor n s actor' n' s',
  s' = state_set_mtx_id s actor n ->
  Owns actor' n' s <-> Owns actor' n' s'.
Proof.
  intros [] [] [mtx_id1 mtx_id2 m1 m2 m3];
    intros [] [] [mtx_id1' mtx_id2' m1' m2' m3'] Es'; rewrite -> Es';
    split; simpl;
    intros HOwns; inversion HOwns;
    subst; simpl in * |-;
    apply owns; assumption.
Qed.

Theorem reachable_implies_possible : forall e,
  initial_executor ==> e -> PossibleExecutor e.
Proof.
  intros e HReachable.
  induction HReachable as [| e e' HReachable IH HStep].
  - apply possible_exec_xl with (actor := Actor1); firstorder;
      apply owns; firstorder.
  - inversion IH as [ e0 actor s actor' mtx_id mtx_id' [Hpc | Hpc] Hpc' Hmtx_id'
                      HOwns HOwnsNext HOwns'
                    | e0 actor s actor' mtx_id mtx_id' Hpc Hpc' Hmtx_id' HFree
                      HOwns HOwns'];
      subst; inversion HStep
          as [ step_pc1 step_pc1' step_pc2 step_s step_s' HTStep
             | step_pc1 step_pc2 step_pc2' step_s step_s' HTStep];
      inversion HTStep
        as [ tstep_actor tstep_s tstep_s' HTStep_cur_mtx_id HTStepLocked
           | tstep_actor tstep_s tstep_s' tstep_s''
             HTStepUnlocked HTStep_cur_mtx_id
           | tstep_actor tstep_s tstep_s' HTStep_s].
    + subst step_s step_pc1 step_pc1' step_s' tstep_actor e e'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * discriminate Hpc.
      * subst actor' step_pc2.
        inversion HTStepLocked as [? ? ? HFree].
        subst. clear Hpc' HTStep_cur_mtx_id.
        remember (current_mtx_id Actor1 tstep_s) as mtx_id eqn:Emtx_id.
        remember (current_mtx_id Actor2 tstep_s) as mtx_id' eqn:Emtx_id'.
        rewrite -> Emtx_id' in Hmtx_id'.
        rewrite -> Hmtx_id' in HFree.
        rewrite <- next_prev_mtx_inv in HFree.
        rewrite <- Emtx_id' in HFree.
        exfalso.
        apply free_xor_owned
          with (actor := Actor2) (n := mtx_id') (s := tstep_s).
        apply HFree. apply HOwns.
    + subst tstep_actor step_pc1 step_s step_pc1' step_s' e e'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * discriminate Hpc.
      * discriminate Hpc'.
    + subst tstep_actor step_pc1 step_s step_pc1' step_s' e e' tstep_s'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * subst. subst actor'. clear Hpc.
        remember (current_mtx_id Actor1 tstep_s) as mtx_id eqn:Emtx_id.
        remember (current_mtx_id Actor2 tstep_s) as mtx_id' eqn:Emtx_id'.
        apply possible_exec_xl
          with (actor := Actor1); firstorder; simpl in * |- *;
          try rewrite <- Emtx_id'; try rewrite <- Emtx_id; auto.
      * subst. subst actor'. discriminate Hpc'.
    + subst tstep_actor step_s step_pc2 step_pc2' e e' tstep_s'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * clear Hpc'. subst step_pc1 actor'. exfalso.
        remember (current_mtx_id Actor1 tstep_s) as mtx_id1 eqn:Emtx_id1.
        remember (current_mtx_id Actor2 tstep_s) as mtx_id2 eqn:Emtx_id2.
        rewrite -> Hmtx_id' in HTStepLocked.
        rewrite <- next_prev_mtx_inv in HTStepLocked.
        inversion HTStepLocked as [htlocked_n htlocked_owner htlocked_s HFree].
        subst htlocked_n htlocked_owner htlocked_s.
        apply free_xor_owned
          with (actor := Actor1) (n := mtx_id1) (s := tstep_s);
          assumption.
      * discriminate Hpc.
    + subst tstep_actor step_pc2 step_s step_pc2' e e' tstep_s''.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *; congruence.
    + subst tstep_actor step_s step_pc2 step_pc2' e e' tstep_s' step_s'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * discriminate Hpc'.
      * clear Hpc. subst. subst actor'.
        apply possible_exec_xl with (actor := Actor2); firstorder.
    + subst tstep_actor step_pc1 step_s step_pc1' step_s' e e'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * discriminate Hpc.
      * subst. subst actor'. clear Hpc'. exfalso.
        remember (current_mtx_id Actor1 tstep_s) as mtx_id1 eqn:Emtx_id1.
        remember (current_mtx_id Actor2 tstep_s) as mtx_id2 eqn:Emtx_id2.
        rewrite -> Hmtx_id' in HTStepLocked.
        rewrite <- next_prev_mtx_inv in HTStepLocked.
        inversion HTStepLocked as [htlocked_n htlocked_owner htlocked_s HFree].
        subst htlocked_n htlocked_owner htlocked_s.
        apply free_xor_owned
          with (actor := Actor2) (n := mtx_id2) (s := tstep_s);
          assumption.
    + subst tstep_actor step_pc1 step_s step_pc1' step_s' e e'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * subst. subst actor'. clear Hpc.
        apply possible_exec_ll with (actor := Actor1);
          firstorder; simpl in * |- *.
        -- rewrite -> current_mtx_id_elim.
           rewrite -> current_mtx_id_elim'.
           rewrite -> double_next_mtx.
           remember (current_mtx_id Actor1 tstep_s) as mtx_id1 eqn:Emtx_id1.
           remember (current_mtx_id Actor2 tstep_s) as mtx_id2 eqn:Emtx_id2.
           rewrite <- unlocked_preserves_ids
             with (owner := Actor1) (n := mtx_id1) (s := tstep_s).
           ++ rewrite <- Emtx_id2.
              rewrite <- unlocked_preserves_ids
                with (owner := Actor1) (n := mtx_id1) (s := tstep_s);
                try rewrite <- Emtx_id1; assumption.
           ++ apply HTStepUnlocked.
           ++ congruence.
        -- rewrite -> current_mtx_id_elim.
           rewrite <- prev_next_mtx_inv.
           apply unlocked_makes_free in HTStepUnlocked as HFree'.
           remember (current_mtx_id Actor1 tstep_s) as mtx_id1 eqn:Emtx_id1.
           apply state_set_mtx_id_keeps_free
             with (actor := Actor1) (n := next_mtx mtx_id1) (s := tstep_s');
               try rewrite <- Emtx_id1;
               rewrite <- unlocked_preserves_ids
                 with (owner := Actor1) (n := mtx_id1) (s := tstep_s);
               congruence.
        -- rewrite -> current_mtx_id_elim.
           remember (current_mtx_id Actor1 tstep_s) as mtx_id1 eqn:Emtx_id1.
           rewrite <- unlocked_preserves_ids
             with (owner := Actor1) (n := mtx_id1) (s := tstep_s); auto.
           apply state_set_mtx_id_keeps_owns
             with (actor := Actor1) (n := next_mtx mtx_id1) (s := tstep_s');
             rewrite <- Emtx_id1; auto.
           apply (unlocked_preserves_owns _ Actor1 mtx_id1 (next_mtx mtx_id1)
                  tstep_s tstep_s').
           ++ apply next_mtx_distinct.
           ++ apply HTStepUnlocked.
           ++ apply HOwnsNext.
        -- rewrite -> current_mtx_id_elim'; try congruence.
           apply state_set_mtx_id_keeps_owns
             with (actor := Actor1)
                  (n := next_mtx (current_mtx_id Actor1 tstep_s'))
                  (s := tstep_s');
             auto.
           apply unlocked_retains_others_owns
             with (actor' := Actor1)
                  (m := current_mtx_id Actor1 tstep_s)
                  (s := tstep_s).
           ++ congruence.
           ++ rewrite <- unlocked_preserves_ids
                with (owner := Actor1)
                     (n := current_mtx_id Actor1 tstep_s)
                     (s := tstep_s).
              ** apply HOwns'.
              ** apply HTStepUnlocked.
           ++ apply HTStepUnlocked.
      * discriminate Hpc'.
    + subst tstep_actor step_pc1 step_s step_pc1' step_s' tstep_s' e e'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * subst. apply possible_exec_xl with (actor := Actor1); firstorder.
      * subst. discriminate Hpc'.
    + subst tstep_actor step_pc2 step_s step_pc2' step_s' e e'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * subst. exfalso. clear Hpc'. subst actor'.
        rewrite -> Hmtx_id' in HTStepLocked.
        rewrite <- next_prev_mtx_inv in HTStepLocked.
        inversion HTStepLocked as [htlocked_n htlocked_owner htlocked_s HFree].
        subst.
        apply free_xor_owned
          with (actor := Actor1)
               (n := current_mtx_id Actor1 tstep_s)
               (s := tstep_s).
        apply HFree. apply HOwns.
      * subst. discriminate Hpc.
    + subst tstep_actor step_pc2 step_s step_pc2' step_s' e e'.
      subst mtx_id mtx_id' s. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * subst. discriminate Hpc'.
      * subst. clear Hpc. subst actor'.
        remember (current_mtx_id Actor2 tstep_s) as mtx_id2 eqn:Emtx_id2.
        apply possible_exec_ll with (actor := Actor2).
        -- reflexivity.
        -- reflexivity.
        -- simpl.
           rewrite -> current_mtx_id_elim, -> current_mtx_id_elim';
             try congruence.
           rewrite -> double_next_mtx.
           rewrite <- unlocked_preserves_ids
             with (owner := Actor2) (n := mtx_id2) (s := tstep_s);
             auto.
           rewrite <- unlocked_preserves_ids
             with (owner := Actor2)
                  (n := mtx_id2)
                  (s := tstep_s)
                  (s' := tstep_s');
             congruence.
        -- simpl.
           rewrite -> current_mtx_id_elim; try congruence.
           rewrite <- prev_next_mtx_inv.
           apply state_set_mtx_id_keeps_free
             with (actor := Actor2)
                  (n := next_mtx (current_mtx_id Actor2 tstep_s'))
                  (s := tstep_s');
             auto.
           apply unlocked_makes_free with (actor := Actor2) (s := tstep_s).
           rewrite <- unlocked_preserves_ids
             with (owner := Actor2) (n := mtx_id2) (s := tstep_s);
             congruence.
        -- simpl.
           rewrite -> current_mtx_id_elim.
           apply state_set_mtx_id_keeps_owns
             with (actor := Actor2)
                  (n := next_mtx (current_mtx_id Actor2 tstep_s'))
                  (s := tstep_s');
             auto.
           rewrite <- unlocked_preserves_ids
             with (owner := Actor2) (n := mtx_id2) (s := tstep_s);
             try congruence.
           rewrite <- Emtx_id2.
           apply (unlocked_preserves_owns _ Actor2 mtx_id2 _ tstep_s _);
             try assumption.
           apply next_mtx_distinct.
        -- simpl.
           rewrite -> current_mtx_id_elim'; try congruence.
           rewrite <- unlocked_preserves_ids
             with (owner := Actor2) (n := mtx_id2) (s := tstep_s);
             auto.
           rewrite <- unlocked_preserves_ids
             with (owner := Actor2)
                  (n := mtx_id2)
                  (s := tstep_s)
                  (s' := tstep_s');
             auto.
           apply state_set_mtx_id_keeps_owns
             with (actor := Actor2) (n := next_mtx mtx_id2) (s := tstep_s');
             try congruence.
           apply unlocked_retains_others_owns
             with (actor' := Actor2) (m := mtx_id2) (s := tstep_s);
             try assumption.
           congruence.
    + subst tstep_actor step_pc2 step_s step_pc2' step_s' tstep_s' e e'.
      subst mtx_id mtx_id' s actor'. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * subst. discriminate Hpc'.
      * subst. discriminate Hpc.
    + subst tstep_actor step_pc1 step_s step_pc1' step_s' e e'.
      subst mtx_id mtx_id' s actor'. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * subst. clear Hpc.
        remember (current_mtx_id Actor1 tstep_s) as mtx_id1 eqn:Emtx_id1.
        remember (current_mtx_id Actor2 tstep_s) as mtx_id2 eqn:Emtx_id2.
        rewrite -> Hmtx_id' in HOwns'.
        inversion HTStepLocked as [htlocked_n htlocked_owner htlocked_s HFree'].
        subst htlocked_n htlocked_owner htlocked_s. exfalso.
        apply free_xor_owned
          with (actor := Actor2) (n := next_mtx mtx_id1) (s := tstep_s);
          assumption.
      * subst. clear Hpc'.
        remember (current_mtx_id Actor1 tstep_s) as mtx_id1 eqn:Emtx_id1.
        remember (current_mtx_id Actor2 tstep_s) as mtx_id2 eqn:Emtx_id2.
        apply possible_exec_xl with (actor := Actor1);
          simpl in * |- *; firstorder.
        -- rewrite <- HTStep_cur_mtx_id.
           rewrite <- locked_preserves_ids
             with (owner := Actor1) (n := next_mtx mtx_id1) (s := tstep_s).
           ++ rewrite <- Emtx_id2. symmetry.
              apply prev_mtx_inv_next_mtx. apply Hmtx_id'.
           ++ apply HTStepLocked.
        -- rewrite <- HTStep_cur_mtx_id.
           apply (locked_preserves_owns _ Actor1 (next_mtx mtx_id1) _ tstep_s);
             try assumption.
           apply not_eq_sym. apply next_mtx_distinct.
        -- rewrite <- HTStep_cur_mtx_id.
           apply locked_implies_owns with (s := tstep_s).
           apply HTStepLocked.
        -- rewrite <- locked_preserves_ids
             with (owner := Actor1) (n := next_mtx mtx_id1) (s := tstep_s).
           ++ apply locked_retains_others_owns
                with (actor' := Actor1) (m := next_mtx mtx_id1) (s := tstep_s);
                congruence.
           ++ apply HTStepLocked.
    + subst tstep_actor step_pc1 step_s step_pc1' step_s' e e'.
      subst mtx_id mtx_id' s actor'. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * subst. discriminate Hpc.
      * discriminate Hpc'.
    + subst tstep_actor step_pc1 step_s step_pc1' step_s' e e' tstep_s'.
      subst mtx_id mtx_id' s actor'. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * discriminate Hpc.
      * discriminate Hpc'.
    + subst tstep_actor step_pc2 step_s step_pc2' step_s' e e'.
      subst mtx_id mtx_id' s actor'. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * subst. clear Hpc'.
        remember (current_mtx_id Actor1 tstep_s) as mtx_id1 eqn:Emtx_id1.
        remember (current_mtx_id Actor2 tstep_s) as mtx_id2 eqn:Emtx_id2.
        apply possible_exec_xl with (actor := Actor2);
          simpl in * |- *; firstorder.
        -- rewrite <- HTStep_cur_mtx_id.
           rewrite <- locked_preserves_ids
             with (owner := Actor2) (n := next_mtx mtx_id2) (s := tstep_s).
           ++ rewrite <- Emtx_id1.
              symmetry. apply prev_mtx_inv_next_mtx.
              apply Hmtx_id'.
           ++ apply HTStepLocked.
        -- rewrite <- HTStep_cur_mtx_id.
           apply (locked_preserves_owns _ Actor2 (next_mtx mtx_id2) _ tstep_s);
             try assumption.
           apply not_eq_sym, next_mtx_distinct.
        -- rewrite <- HTStep_cur_mtx_id.
           apply locked_implies_owns with (s := tstep_s).
           assumption.
        -- rewrite <- locked_preserves_ids
             with (owner := Actor2) (n := next_mtx mtx_id2) (s := tstep_s).
           ++ rewrite <- Emtx_id1.
              apply locked_retains_others_owns
                with (actor' := Actor2) (m := next_mtx mtx_id2) (s := tstep_s);
                congruence.
           ++ assumption.
      * subst. clear Hpc.
        remember (current_mtx_id Actor1 tstep_s) as mtx_id1 eqn:Emtx_id1.
        remember (current_mtx_id Actor2 tstep_s) as mtx_id2 eqn:Emtx_id2.
        inversion HTStepLocked as [htlocked_n htlocked_owner htlocked_s HFree'].
        subst htlocked_n htlocked_owner htlocked_s.
        exfalso.
        apply free_xor_owned
          with (actor := Actor1) (n := next_mtx mtx_id2) (s := tstep_s).
        apply HFree'. rewrite <- Hmtx_id'. apply HOwns'.
    + subst tstep_actor step_pc2 step_s step_pc2' step_s' e e'.
      subst mtx_id mtx_id' s actor'. simpl in * |-.
      destruct actor eqn:Eactor; simpl in * |- *.
      * discriminate Hpc'.
      * discriminate Hpc.
    + subst tstep_actor step_pc2 step_s step_pc2' step_s' tstep_s' e e'.
      subst mtx_id mtx_id' s actor'.
      destruct actor eqn:Eactor; simpl in * |- *.
      * discriminate Hpc'.
      * discriminate Hpc.
Qed.

Inductive ValidState : State -> Prop :=
  | initial_state_valid :
      ValidState (state Mtx1 Mtx3 (MtxL Actor1) (MtxL Actor1) (MtxL Actor2))
  | valid_state_unlock owner n (s s' : State) (H : ValidState s) :
      Owns owner n s ->
      Owns owner (next_mtx n) s ->
      current_mtx_id owner s = n ->
      current_mtx_id owner s' = next_mtx n ->
      Free n s' ->
      ValidState s'
  | valid_state_lock owner n (s s' : State) (H : ValidState s) :
      Owns owner n s ->
      Locked owner (next_mtx n) s s' ->
      current_mtx_id owner s = n ->
      current_mtx_id owner s = current_mtx_id owner s' ->
      ValidState s'.

Definition preservation := forall e,
  initial_executor ==> e ->
  ValidState (executor_state e).

Theorem preservation_satisfied : preservation.
Proof.
  intros e HReachable.
  induction HReachable as [| e e' HReachable IH HStep].
  - simpl. apply initial_state_valid.
  - apply reachable_implies_possible in HReachable as HPossible.

    inversion HPossible
      as [ e0 actor s actor' mtx_id mtx_id' [Hpc | Hpc] Hpc' Hmtx_id' HOwns
           HOwnsNext HOwns'
         | e0 actor s actor' mtx_id mtx_id' Hpc Hpc' Hmtx_id' HFree HOwns
           HOwns'];
      subst e0 s mtx_id mtx_id' actor';
      inversion HStep
        as [ step_pc1 step_pc1' step_pc2 step_s step_s' HTStep
           | step_pc1 step_pc2 step_pc2' step_s step_s' HTStep];
      inversion HTStep
        as [ tstep_actor tstep_s tstep_s' Emtx_id HTStepLocked
           | tstep_actor tstep_s tstep_s' tstep_s'' HTStepUnlocked Es''
           | tstep_actor tstep_s tstep_s' Es];
      subst e e';
      subst tstep_actor tstep_s;
      try subst tstep_s';
      try subst step_pc1;
      try subst step_pc2;
      try subst step_pc1';
      try subst step_pc2';
      destruct actor;
      simpl in * |- *;
      subst;
      try discriminate Hpc;
      try discriminate Hpc';
      try (remember (current_mtx_id Actor1 step_s) as mtx_id1 eqn:Emtx_id1;
           remember (current_mtx_id Actor2 step_s) as mtx_id2 eqn:Emtx_id2);
      auto.

    + clear Hpc'.
      apply (valid_state_lock Actor1 mtx_id1 step_s); auto.
      apply locked_preserves_ids with (owner := Actor1) (n := next_mtx mtx_id1).
      assumption.
    + clear Hpc'.
      rewrite -> Hmtx_id' in HTStepLocked.
      rewrite <- next_prev_mtx_inv in HTStepLocked.
      inversion HTStepLocked as [? ? ? HFree].
      exfalso. apply (free_xor_owned Actor1 mtx_id1 step_s); assumption.
    + clear Hpc'.
      apply (valid_state_lock Actor1 mtx_id1 step_s); auto.
      apply locked_preserves_ids with (owner := Actor1) (n := next_mtx mtx_id1).
      assumption.
    + clear Hpc.
      rewrite <- unlocked_preserves_ids
        with (owner := Actor1) (n := mtx_id1) (s := step_s); auto.
      rewrite <- Emtx_id1.
      apply (valid_state_unlock Actor1 mtx_id1 step_s); auto.
      * rewrite -> current_mtx_id_elim. reflexivity.
      * apply state_set_mtx_id_keeps_free
          with (actor := Actor1) (n := next_mtx mtx_id1) (s := tstep_s'); auto.
        apply unlocked_makes_free with (actor := Actor1) (s := step_s). auto.
    + clear Hpc'.
      rewrite -> Hmtx_id' in HTStepLocked.
      rewrite <- next_prev_mtx_inv in HTStepLocked.
      inversion HTStepLocked as [? ? ? HFree].
      exfalso. apply (free_xor_owned Actor1 mtx_id1 step_s); assumption.
    + clear Hpc.
      rewrite <- unlocked_preserves_ids
        with (owner := Actor2) (n := mtx_id2) (s := step_s); auto.
      rewrite <- Emtx_id2.
      apply (valid_state_unlock Actor2 mtx_id2 step_s); auto.
      * rewrite -> current_mtx_id_elim. reflexivity.
      * apply state_set_mtx_id_keeps_free
          with (actor := Actor2) (n := next_mtx mtx_id2) (s := tstep_s'); auto.
        apply unlocked_makes_free with (actor := Actor2) (s := step_s). auto.
    + clear Hpc.
      apply (valid_state_lock Actor1 mtx_id1 step_s); auto.
      apply locked_preserves_ids with (owner := Actor1) (n := next_mtx mtx_id1).
      assumption.
    + clear Hpc'.
      apply (valid_state_lock Actor1 mtx_id1 step_s); auto.
      apply locked_preserves_ids with (owner := Actor1) (n := next_mtx mtx_id1).
      assumption.
    + clear Hpc'.
      apply (valid_state_lock Actor2 mtx_id2 step_s); auto.
      apply locked_preserves_ids with (owner := Actor2) (n := next_mtx mtx_id2).
      assumption.
    + clear Hpc.
      apply (valid_state_lock Actor2 mtx_id2 step_s); auto.
      apply locked_preserves_ids with (owner := Actor2) (n := next_mtx mtx_id2).
      assumption.
Qed.

Definition next_step (e : Executor) : Executor :=
  match e with
  | executor pc1 pc2 s =>
      match s with
      | state mtx_id1 mtx_id2 m1 m2 m3 =>
          let n1 := next_mtx mtx_id1
          in let n2 := next_mtx mtx_id2
          in match pc1, pc2 with
             | PCLock, PCLock =>
                 if mtx_eqv (state_get_mtx s n1) MtxU
                 then executor PCPrint PCLock (state_set_mtx s n1 (MtxL Actor1))
                 else executor PCLock PCPrint (state_set_mtx s n2 (MtxL Actor2))
             | PCPrint, PCLock =>
                 executor PCUnlock PCLock s
             | PCUnlock, PCLock =>
                 executor PCLock PCLock
                   (state_set_mtx_id (state_set_mtx s mtx_id1 MtxU) Actor1 n1)
             | PCLock, PCPrint =>
                 executor PCLock PCUnlock s
             | PCPrint, PCPrint => e
             | PCUnlock, PCPrint => e
             | PCLock, PCUnlock =>
                 executor PCLock PCLock
                   (state_set_mtx_id (state_set_mtx s mtx_id2 MtxU) Actor2 n2)
             | PCPrint, PCUnlock => e
             | PCUnlock, PCUnlock => e
             end
      end
  end.

Definition progress := forall e,
  initial_executor ==> e ->
  e ~~> next_step e.

Theorem progress_satisfied : progress.
Proof.
  intros [pc1 pc2 [mtx_id1 mtx_id2 m1 m2 m3]] HReachable.
  remember (state mtx_id1 mtx_id2 m1 m2 m3) as s eqn:Es.
  apply reachable_implies_possible in HReachable as HPossible.
  inversion HPossible
    as [ e0 actor s0 actor' mtx_id mtx_id' [Hpc | Hpc] Hpc' Hmtx_id' HOwns
         HOwnsNext HOwns'
       | e0 actor s0 actor' mtx_id mtx_id' Hpc Hpc' Hmtx_id' HFree HOwns
         HOwns'];
    subst e0 s0 mtx_id mtx_id' actor';
    destruct actor; simpl in * |- *; subst.

  - apply exec_thread1. apply thread_pc_print. reflexivity.
  - apply exec_thread2. apply thread_pc_print. reflexivity.
  - apply exec_thread1.
    remember (state_set_mtx _ _ _) as s' eqn:Es'.
    apply thread_pc_unlock with (s' := s'); subst s'.
    + apply unlocked. apply HOwns.
    + simpl. destruct mtx_id1; auto.
  - apply exec_thread2.
    remember (state_set_mtx _ _ _) as s' eqn:Es'.
    apply thread_pc_unlock with (s' := s'); subst s'.
    + apply unlocked. apply HOwns.
    + simpl. destruct mtx_id2 eqn:Emtx_id2; simpl; auto.
  - pose (s := state mtx_id1 mtx_id2 m1 m2 m3).
    destruct mtx_id1 eqn:Emtx_id1, mtx_id2 eqn:Emtx_id2; simpl in * |- *;
      try discriminate Hmtx_id';
      remember (state_get_mtx s mtx_id2) as m eqn:Em;
      unfold s in Em; rewrite -> Emtx_id2 in Em; simpl in Em;
      rewrite <- Em; destruct (mtx_eqv m MtxU) eqn:Emeq;
      try (apply mtx_eqv_eq in Emeq; rewrite -> Emeq; apply exec_thread1);
      try apply exec_thread2;
      apply thread_pc_lock;
      try apply locked;
      try apply free;
      auto; simpl.
    all: inversion HFree; auto.
  - pose (s := state mtx_id1 mtx_id2 m1 m2 m3).
    destruct mtx_id1 eqn:Emtx_id1, mtx_id2 eqn:Emtx_id2; simpl in * |- *;
      try discriminate Hmtx_id';
      inversion HFree as [free_n free_s Heq];
      subst free_n free_s; simpl in Heq; rewrite -> Heq in * |- *;
      simpl;
      apply exec_thread1;
      apply thread_pc_lock; auto;
      apply locked;
      apply HFree.
Qed.

Theorem correct : preservation /\ progress.
Proof.
  split.
  - apply preservation_satisfied.
  - apply progress_satisfied.
Qed.

Print preservation.
Print progress.
