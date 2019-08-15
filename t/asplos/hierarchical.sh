#!/bin/bash
# set -x

root="no"
nnodes=2
ncores=4

#
declare -r prog=${0##*/}
die() { echo -e "$prog: $@"; exit 1; }

#
declare -r long_opts="help,root,nnodes:,ncores-per-node:"
declare -r short_opts="hrN:C:"
declare -r usage="
Usage: $prog [OPTIONS] -- [CONFIGURE_ARGS...]\n\
Distribute and schedule jobs through the flux instance hierarchy\n\
in accordance with a fully hierarchical scheduling policy.\n\
\n\
Options:\n\
 -h, --help                    Display this message\n\
 -r, --root                    Indicate that the instance is the root (only for SLURM)\n\
 -N, --nnodes                  Total num of nodes allocated to this instance (default=${nnodes})\n\
 -C, --ncores-per-node         Total num of cores per node allocated to this instance(default=${ncores})\n\
CONFIGURE_ARGS\n\
 ARG1                          Sub-hierarchy shape rooted at this instance\n\
 ARG2                          Hierarchy prefix leading up to this instance\n\
"

GETOPTS=`/usr/bin/getopt -u -o ${short_opts} -l ${long_opts} -n ${prog} -- ${@}`
if [[ $? != 0 ]]; then
    die "${usage}"
fi
eval set -- "${GETOPTS}"

while true; do
    case "${1}" in
      -h|--help)                   echo -ne "${usage}";          exit 0  ;;
      -r|--root)                   root="yes";                   shift 1 ;;
      -N|--nnodes)                 nnodes=${2};                  shift 2 ;;
      -C|--ncores-per-node)        ncores=${2};                  shift 2 ;;
      --)                          shift; break;                         ;;
      *)                           die "Invalid option '${1}'\n${usage}" ;;
    esac
done

if [ $# -ne 2 ]; then
    die ${usage}
fi

if [ ${root} = "yes" ]; then
    echo "srun -N ${i} -n ${i} flux start ${prog} ${i}"
    exit 0
fi

prefix=${2}
policy=${1}
begin=0
end=0
next=""
level=`echo ${1} | awk -F"x" '{print NF}'`

for i in `seq 2 ${level}`
do
    token=$(echo ${policy} | cut -d'x' -f${i})
    next="${next:+${next}"x"}${token}"
done

if [ ${level} -eq 1 ]; then
    # Leaf level
    jobid=0
    mkdir -p ${prefix}
    begin=$(date +%s.%N) 
    jobids=""

    for i in `seq 1 ${1}`;
    do
        flux jobspec srun -n 1 sleep 0 > ${prefix}/sleep-${prefix}.${i}.json
        jobid=$(flux job submit ${prefix}/sleep-${prefix}.${i}.json)
	jobids="${jobids} ${jobid}"
    done

    for j in ${jobids};
    do
        flux job wait-event -t 216000 ${jobid} clean
    done 
    end=$(date +%s.%N) 
else
    # Intermediate Level
    jobid=""
    jobids=""
    size=$(echo ${1} | awk -F"x" '{print $1}')
    total_cores=$(( nnodes*ncores ))
    t_core_share=$(( total_cores/size ))
    t_core_rem=$(( total_cores%size ))
    node_share=$(( t_core_share/ncores ))
    core_share=0
    if [ ${node_share} -ge 1 ]; then
        core_share=$(( t_core_share/node_share ))
    else
        node_share=1
        core_share=${t_core_share}
    fi
    core_share_imb=$(( core_share+t_core_rem ))
    mkdir -p ${prefix}

    #
    # Run intermediate flux instances
    #
    begin=$(date +%s.%N)

#    flux jobspec srun -N ${node_share} -n ${node_share} -c ${core_share_imb} \
#flux start ./${prog} -N ${node_share} -C ${core_share_imb} \
#${next} ${prefix}.1 > ${prefix}/flux.${prefix}.1.json
#    jobids=$(flux job submit ${prefix}/flux.${prefix}.1.json)
#    jobids="${jobids} "

#    for i in `seq 2 ${size}`;
    for i in `seq 1 ${size}`;
    do
	echo ${prefix}.${i}
        flux jobspec srun -N ${node_share} -n ${node_share} -c ${core_share} \
flux start ./${prog} -N ${node_share} -C ${core_share} \
${next} ${prefix}.${i} > ${prefix}/flux.${prefix}.${i}.json
        jobid=$(flux job submit ${prefix}/flux.${prefix}.${i}.json)
	jobids="${jobids} ${jobid}"
    done
   
    for j in ${jobids};
    do
        flux job wait-event -t 216000 ${j} clean
    done

    end=$(date +%s.%N)
    #
    # End 
    #
fi

elapse=$(python -c "print ($end-$begin)")
printf "%-30s %-30s %-30s\n" "Start" "End" "Elapse" > ${prefix}/perf.${prefix}.out
printf "%-30s %-30s %-30s\n" "${begin}" "${end}" "${elapse}" >> ${prefix}/perf.${prefix}.out

