#!/usr/bin/ruby

require 'fileutils'

@options = { 
  :target_bound => 30,
  :rewrite_bound => 30,
  :shadow => false,
}

@rodata_needed = {
  "s000"      => ["gcc", "llvm"],
  "s1112"     => ["gcc", "llvm"],
  "s116"      => ["gcc", "llvm"],
  "s1221"     => ["gcc"],
  "s122"      => ["gcc"],
  "s315"      => ["baseline", "gcc", "llvm"],
  "s318"      => ["baseline", "gcc", "llvm"],
  "s3251"     => ["llvm"],
  "s351"      => ["baseline", "gcc", "llvm"],
  "s452"      => ["gcc"],
  "s453"      => ["gcc"],
  "stacktest" => ["gcc", "llvm"],
  "testing"   => ["baseline", "gcc", "llvm"],
}

@default_def_ins = "\"{ %rdi %rbp %rsp %rbx %r12 %r13 %r14 %r15 }\""
@default_live_outs = "\"{ %rbx %rsp %rbp %r12 %r13 %r14 %r15 }\""

def print_usage
  puts "usage: ./demo.rb verify [options] <compiler1> <compiler2> <benchmark>"
  puts "       ./demo.rb verify-all [options] <list>"
  puts "       ./demo.rb verify-gcc [options] <list>"
  puts "       ./demo.rb verify-llvm [options] <list>"
  puts "       ./demo.rb check-tc-all"
  puts ""
  puts "Options:"
  puts ""
  puts "  --target-bound n"
  puts "  --rewrite-bound n"
  puts ""
end

def check_file(s)
  if !File.exist?(s) then
    print_usage
    puts "Could not find file #{s}"
    exit 1
  end
end
  
def validate_all(filename, compiler)
  File.readlines(filename).each do |line|
    benchmark = line.strip
    if compiler == :all or compiler == :gcc then
      validate "baseline", "gcc", benchmark, true
    end
    if compiler == :all or compiler == :llvm then
      validate "baseline", "llvm", benchmark, true
    end
  end
  Process.waitall
end

def check_all_testcases(filename)
  File.readlines(filename).each do |line|
    benchmark = line.strip
    check_testcases 'baseline', benchmark, 'testcases/16'
    check_testcases 'gcc',      benchmark, 'testcases/16'
    check_testcases 'llvm',     benchmark, 'testcases/16'
  end
end

def check_testcases(compiler1, benchmark, tcfile)

  (0..15).each do |index|
    args = [
      "--testcases #{tcfile}",
      "--target #{compiler1}/#{benchmark}.s",
      "--index #{index}"
    ]

    stoke_cmd = "stoke_debug_sandbox #{args.join(" ")}"
    output = `#{stoke_cmd}`
#print output
    if output.include?("Control returned abnormally")
      puts "#{compiler1}/#{benchmark} BAD index=#{index}"
      return false
    end
      
  end
  puts "#{compiler1}/#{benchmark} GOOD"
  return true
end

def validate(compiler1, compiler2, benchmark, dofork=false) 
  puts "Running benchmark #{benchmark} with compilers #{compiler1}/#{compiler2}"
  FileUtils.mkdir_p 'misc'
  FileUtils.mkdir_p 'traces'
  FileUtils.mkdir_p 'times'

  num = 0
  prefix = "#{benchmark}_#{compiler1}_#{compiler2}"
  name = "#{prefix}.#{num}"

  if benchmark != "s176" then #the s176 benchmark has a doubly-nested loop, so adding
                              #testcases can be really expensive
    testcase_file = "testcases/256"
  else
    testcase_file = "testcases/128"
  end

  check_file "#{compiler1}/#{benchmark}.s"
  check_file "#{compiler2}/#{benchmark}.s"
  check_file testcase_file
  check_file "rodata"

  rodata_compilers = @rodata_needed[benchmark]
  rodata_needed = (not rodata_compilers.nil?) and (rodata_compilers.include?(compiler1) or rodata_compilers.include?(compiler2))

  while File.exist?("traces/#{name}")
    num = num+1
    name = "#{prefix}.#{num}"
  end

  live_outs = @default_live_outs
  if benchmark == "sum1d" then
    live_outs = "\"{ %rax %rbx %rsp %rbp %r12 %r13 %r14 %r15 }\""
  end
  if benchmark == "vpvts" then
    def_ins = "\"{ %rdi %rsi %rbp %rsp %rbx %r12 %r13 %r14 %r15 }\""
  end

  stoke_args = [
    "--strategy ddec",
    "--solver z3",
    "--alias_strategy flat",
    "--target #{compiler1}/#{benchmark}.s",
    "--rewrite #{compiler2}/#{benchmark}.s",
    "--testcases #{testcase_file}",
    "--vector_invariants",
    "--heap_out",
    "--max_jumps 129000",
    "--live_out #{live_outs}",
    "--def_in #{@default_def_ins}",
    "--target_bound #{@options[:target_bound]}",
    "--rewrite_bound #{@options[:rewrite_bound]}",
    "--assume \"(t_%rdi<=15)\"",
  ]

  if @options[:shadow] then
    stoke_args.push("--shadow_registers") 
  end

  if rodata_needed
    stoke_args.push("--rodata rodata")
  end

  puts "Recording data in traces/#{name}"
  io = "2> misc/#{name}.err | tee traces/#{name}"
  stoke_cmd = "stoke_debug_verify #{stoke_args.join(" ")}"
  time_cmd = "/usr/bin/time -o times/#{name} #{stoke_cmd} #{io}"

  File.open("misc/#{name}.cmd", 'w') do |file|
    file.write(stoke_cmd)
  end

  puts time_cmd
  if dofork
    Process.fork do
      `#{time_cmd}`
    end
  else
    `#{time_cmd}`
  end
end

def update_options
  n=1
  while ARGV[n].start_with?("-") do
    if ARGV[n] == "--target-bound" then
      n = n+1
      @options[:target_bound] = ARGV[n].to_i
    end
    if ARGV[n] == "--rewrite-bound" then
      n = n+1
      @options[:rewrite_bound] = ARGV[n].to_i
    end
    if ARGV[n] == "--shadow-registers" then
      @options[:shadow] = true
    end
    n = n+1
  end
  n
end

if ARGV[0] == "verify" then
  n = update_options
  if ARGV.size == n+3 then 
    validate ARGV[n], ARGV[n+1], ARGV[n+2]
  else
    print_usage
  end
elsif ARGV[0] == "verify-all" then
  n = update_options
  validate_all ARGV[n], :all
  while true do
    sleep 10
  end
elsif ARGV[0] == "verify-llvm" then
  n = update_options
  validate_all ARGV[n], :llvm
  while true do
    sleep 10
  end
elsif ARGV[0] == "verify-gcc" then
  n = update_options
  validate_all ARGV[n], :gcc
  while true do
    sleep 10
  end
elsif ARGV[0] == "check-tc-all" then
  check_all_testcases ARGV[1]
else
  print_usage
end
