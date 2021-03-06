open Ocamlbuild_plugin;;
open Command;;

module Util = struct
  let split s ch =
    let x = ref [] in
    let rec go s =
      let pos = String.index s ch in
      x := (String.before s pos)::!x;
      go (String.after s (pos + 1))
    in
    try
      go s
    with Not_found -> !x

    let split_nl s = split s '\n'
    let run_and_read x = List.hd (split_nl (Ocamlbuild_pack.My_unix.run_and_read x))
end

let query dir = Util.run_and_read ("ocamlfind query " ^ dir);;

dispatch begin function
| After_rules ->
    flag ["camlp4of"; "build_syntax"]
	(S[A "-I"; A "+camlp4"]);
    flag ["compile"; "use_custom_stdlib"]
	(S[A "-nostdlib"; A "-I"; A (query "mirage-stdlib")]);
| _ -> ()
end;;
