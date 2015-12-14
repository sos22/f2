{-# LANGUAGE ScopedTypeVariables #-}

import Data.List
import Numeric
import System.Cmd.Utils
import System.Environment
import System.Exit
import Text.Regex

first :: (a -> b) -> (a, c) -> (b, c)
first what (x, y) = (what x, y)

second :: (a -> b) -> (c, a) -> (c, b)
second what (x, y) = (x, what y)


parseline = mkRegex "^[0-9a-f]* ([0-9a-f]*) [dDgGiIrRtTvVwW] (.*)"


encoding :: String -> String
encoding ('_':'Z':what) = decode what
         where decode ('L':r) = r
               decode ('N':r) = getfirstelem r
               decode ('Z':r) = decode r
               decode ('T':'I':r) = "typeinfo name " ++ (getfirstelem r)
               decode ('T':'S':r) = "typeinfo struct " ++ (getfirstelem r)
               decode ('T':'V':r) = "vtable " ++ (getfirstelem r)
               decode ('n':'e':_) = "!="
               decode ('e':'q':_) = "=="
               decode ('n':'w':_) = "new"
               decode ('l':'i':_) = "\"\""
               decode ('o':'o':_) = "||"
               decode ('p':'l':_) = "+"
               decode ('S':'t':x) = "std::" ++ (decode x)
               decode x | head x `elem` "123456789" = getfirstelem x
               decode x = error $ "can't decode " ++ x
               getfirstelem ('S':'t':x) = "std::" ++ (getfirstelem x)
               getfirstelem ('K':x) = getfirstelem x
               getfirstelem ('U':'l':_) = "lambda"
               getfirstelem ('N':x) = getfirstelem x
               getfirstelem ('Z':x) = decode x
               getfirstelem str = case readDec str of
                                       [(len, rest)] -> take len rest
                                       _ -> error $ "can't parse " ++ str
encoding x = x

main :: IO ()
main =
   do args <- getArgs
      (handle, lines) <- case args of
               [path] -> pipeLinesFrom "/usr/bin/nm" ["-S", "--size-sort", path]
               _ -> do print "need precisely one argument"
                       exitWith $ ExitFailure 1
      let ps = flip concatMap lines $ \l ->
                    case matchRegex parseline l of
                         Nothing -> []
                         Just [size, name] ->
                              [(fst $ head  $ readHex size,
                                name)]
          pss = map (second encoding) ps
          srted = sortBy (\(_, a) (_, b) -> a `compare` b) pss
          acced = foldr (\(sz, name) (results, acc, lastname) ->
                               if name == lastname
                               then (results, acc + sz, name)
                               else ((acc, lastname):results, sz, name))
                        ([], 0, "")
                        srted
          acced' = case acced of
                        (results, lasti, lastname) -> (lasti,lastname):results
          srtedacc = sortBy (\(a, _) (b, _) -> a `compare` b) acced'
      sequence_ $ flip map srtedacc $ print
