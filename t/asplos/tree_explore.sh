#!/bin/bash

NNODES=128
NCORES=48
outdir="/usr/src/t/asplos"

jobcnts="1024 2048 4092 8192 16384 32768 65536 131072"
topos="1 4 4x4 4x4 4x4x4 4x4x4x4"

num_ll_schedulers() {
    local policy=${1}
    local level=$(echo ${policy} | awk -F"x" '{print NF}')
    local cnt=1
    for i in `seq 1 ${level}`
    do
        token=$(echo ${policy} | cut -d"x" -f${i})
        cnt=$(( cnt*token ))
    done
    echo ${cnt}
}


for topo in ${topos}
do
    dir="topo.${topo}"
    mkdir -p ${dir}
    echo "#!/bin/bash" > ${dir}/topo.${topo}.sh
    echo "#SBATCH -N 128" >> ${dir}/topo.${topo}.sh
    echo "#SBATCH -t 16:00:00" >> ${dir}/topo.${topo}.sh
    echo "#SBATCH -o ${outdir}/${dir}" >> ${dir}/topo.${topo}.sh
    echo "" >> ${dir}/topo.${topo}.sh
    echo "cd ${outdir}/${dir}" >> ${dir}/topo.${topo}.sh

    for cnt in ${jobcnts}
    do
        nscheds=$(num_ll_schedulers ${topo})
        max=$(( nscheds*4096 ))
        if [ ${cnt} -le ${max} ]
        then
            perfout=$(printf "perf.%04d.out" ${nscheds}) 
            echo "flux tree -T${topo} -N${NNODES} -C${NCORES} -J${cnt} -o ${perfout} ../sleep0.sh" >> ${dir}/topo.${topo}.sh
        fi
    done
    chmod u+x ${dir}/topo.${topo}.sh
done
