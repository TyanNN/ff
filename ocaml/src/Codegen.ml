open Llvm;;
open AST;;

module BuiltFnc = struct
  type t = { fnc: llvalue; ret_value: llvalue option; ret_block: llbasicblock; fnc_def: FncDef.t }
end;;

module DefinedVar = struct
  type t = { tp: ttype; instance: llvalue }
end;;


let codegen ast =
  let context = global_context () in
  let modl = create_module context "stdin" in
  let builder = builder context in

  let functions = Hashtbl.create 1 in
  let curr_variables = Hashtbl.create 0 in
  let curr_fn_name = ref "" in

  let get_llvm_type tp =
    match tp with
    | Int -> i32_type context
    | Float -> float_type context
    | Str -> pointer_type @@ i8_type context
    | Void -> void_type context
  in

  let rec expr_type ex =
    let open Expression in
    match ex with
    | IntLit x ->  Int
    | FloatLit x ->  Float
    | StrLit x ->  Str
    | Ident x -> let open AST.Ident in
                 (Hashtbl.find curr_variables x.value).DefinedVar.tp
    | FncCall x -> let open AST.FncCall in
                   (Hashtbl.find functions !curr_fn_name).BuiltFnc.fnc_def.ret_t

  and mangle_type_name ltp = match (classify_type ltp) with
    | Llvm.TypeKind.Struct -> begin
        match struct_name ltp with
        | Some nm -> Printf.sprintf "%i%s" (String.length nm) nm
        | None -> assert false
      end
    | _ -> begin
        let ltp_s = string_of_lltype ltp in
        Printf.sprintf "%i%s" (String.length ltp_s) ltp_s
      end

  and mangle fn =
    let trav_ar_arr x = Array.fold_left (fun acc (_, tp) ->
                             let ltp = get_llvm_type tp in
                             Printf.sprintf "%sA%s" acc (mangle_type_name ltp)) "" x in

    let (name, arg_str, ret_t) = match fn with
      | FncDef { FncDef.name; args; ret_t; _ } | OperatorDef { FncDef.name; args; ret_t; _ } -> (name, trav_ar_arr args, ret_t)
      | _ -> assert false
    in

    let ret_s =
      let ltp_s = string_of_lltype @@ get_llvm_type ret_t in
      Printf.sprintf "R%i%s" (String.length ltp_s) ltp_s
    in
    Printf.sprintf "_FFN%i%s%s%s" (String.length name) name arg_str ret_s

  and mangle_call ca =
    let name = ca.FncCall.name in
    let ar_str =
      let ar_types = List.map expr_type ca.args in
      List.fold_left (fun acc tp ->
          let ltp = get_llvm_type tp in
          Printf.sprintf "%sA%s" acc (mangle_type_name ltp)) "" ar_types
    in
    let ret_s =
      let s = (string_of_lltype @@ get_llvm_type @@ expr_type @@ FncCall ca) in
      Printf.sprintf "R%i%s" (String.length s) s
    in
    Printf.sprintf "_FFN%i%s%s%s" (String.length name) name ar_str ret_s
  in

  let string_of_fnc_call f =
    let ar_l = List.map (fun x -> string_of_ttype @@ expr_type x) f.FncCall.args in
    let ar_s = String.concat ", " ar_l in
    let ret_s = string_of_ttype @@ expr_type @@ FncCall f in
    Printf.sprintf "%s(%s) %s" f.name ar_s ret_s
  in

  let rec gen_expr ex =
    let open AST.Expression in
    match ex with
    | IntLit x -> let open AST.Int in
                  const_int (get_llvm_type Int) x.value
    | FloatLit x -> let open AST.Float in
                    const_float (get_llvm_type Float) x.value
    | StrLit x -> let open AST.Str in
                  const_string context x.value
    | Ident x -> begin
        let open AST.Ident in
        try
          let var = Hashtbl.find curr_variables x.value in
          build_load var.DefinedVar.instance "" builder
        with Not_found ->
          failwith (Printf.sprintf "Undefined variable: %s" x.value)
      end
    | FncCall x -> let open AST.FncCall in
                   try
                     let f = Hashtbl.find functions (mangle_call x) in
                     let args = Array.of_list @@ List.map gen_expr x.args in
                     build_call f.BuiltFnc.fnc args x.name builder
                   with Not_found ->
                     failwith (Printf.sprintf "Undefined function: %s" (string_of_fnc_call x))
  in

  let gen_stmt st =
    match st with
    | Decl x -> begin
        let open AST.Decl in
        let alloca = build_alloca (get_llvm_type x.tp) x.name builder in

        begin match x.value with
        | Some v -> begin
            let vtp = expr_type v
            and atp = x.tp in
            if vtp <> x.tp then
              failwith @@ Printf.sprintf "Wrong type of expression for `%s`, expected %s, but got %s" x.name (string_of_ttype atp) (string_of_ttype vtp)
            else
              ignore(build_store (gen_expr v) alloca builder)
          end
        | None -> ()
        end;
        Hashtbl.replace curr_variables x.name { DefinedVar.instance = alloca; tp = x.tp };
      end
    | ExprAsStmt x -> ignore(gen_expr x)
    | Ret x -> begin
        let open AST.Ret in
        let f = Hashtbl.find functions !curr_fn_name in
        begin
          match x.value with
          | None -> ()
          | Some va ->
             let ret_v = match f.BuiltFnc.ret_value with
               | Some v -> v
               | None -> failwith "Can't return value from void function" in
             ignore(build_store (gen_expr va) ret_v builder)
        end;
        ignore(build_br f.BuiltFnc.ret_block builder)
      end
  in

  List.iter (fun node ->
      Hashtbl.reset curr_variables;
      match node with
      | FncDef x | OperatorDef x -> begin
          let open AST.FncDef in
          let m_name = if x.name <> "main" then mangle node else x.name in
          curr_fn_name := m_name;
          let ftype = function_type (get_llvm_type x.FncDef.ret_t) (Array.map (fun (x,y) -> get_llvm_type y) x.args) in
          let fnc = define_function m_name ftype modl in
          position_at_end (entry_block fnc) builder;

          let ret_block = append_block context "exit" fnc in
          let ret_value = match x.FncDef.ret_t with
            | Void -> position_at_end ret_block builder;
                      ignore(build_ret_void builder);
                      None
            | va ->
               let r = build_alloca (get_llvm_type va) "ret_val" builder in
               position_at_end ret_block builder;
               ignore(build_ret (build_load r "" builder) builder);
               Some r in
          position_at_end (entry_block fnc) builder;

          position_at_end (entry_block fnc) builder;

          (* Make variables from arguments *)

          Array.iteri (fun i par ->
              let r_param = Array.get x.args i in
              let p = build_alloca (type_of par) (fst r_param) builder in
              ignore(build_store par p builder);
              Hashtbl.replace curr_variables (fst r_param) { DefinedVar.tp = snd r_param; instance = p };
            ) (params fnc);

          Hashtbl.replace functions m_name { BuiltFnc.fnc; ret_value; ret_block; fnc_def = x };
          List.iter gen_stmt x.body;
        end
    ) ast; modl;;