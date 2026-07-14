-- | Proves the Haskell -> libmtlc FFI actually links and runs: builds the IR
-- for `int main(void) { return 42; }` and emits a real PE, without going
-- anywhere near the C99 front half.
--
--   ghc -ihs/src hs/test/LinkSmoke.hs libmtlc/lib/mtlc.lib -ldbghelp -o bin/linksmoke.exe
--   bin/linksmoke.exe && bin/smoke42.exe; echo $?   # expect 42
module Main (main) where

import System.Exit (exitFailure)

import Mtlc

main :: IO ()
main = do
  v <- version
  putStrLn ("backend: " ++ v)

  b <- builderCreate
  i32 <- tyScalar I32
  mfn <- builderFunction b "main" i32 []
  case mfn of
    Nothing -> putStrLn "mtlc_builder_function failed" >> exitFailure
    Just fn -> do
      c <- constInt fn i32 42
      ret fn c
      mmod <- builderFinish b
      case mmod of
        Nothing -> putStrLn "mtlc_builder_finish failed" >> exitFailure
        Just m -> do
          n <- moduleFunctionCount m
          putStrLn ("functions: " ++ show n)
          ctx <- contextCreate
          contextSetOptLevel ctx 0
          ok <- buildExecutable ctx m "bin/smoke42.exe"
          moduleDestroy m
          contextDestroy ctx
          if ok
            then putStrLn "wrote bin/smoke42.exe"
            else putStrLn "mtlc_build_executable failed" >> exitFailure
