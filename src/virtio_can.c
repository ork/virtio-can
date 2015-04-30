/* A CAN bus controller driver using virtio.
 *
 * Copyright 2015 Benoît Taine <benoit.taine@openwide.fr> OpenWide Ingénierie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/netdevice.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/virtio.h>
#include <linux/virtio_can.h>

/* Communication over virtio-can uses little endian values, according to the
 * virtio spec.
 *
 * Portions of code copied or inspired from virtio_net.c and flexcan.c
 *
 */

/* Structure of the message buffer */
struct virtcan_mb {
	u32 can_ctrl;
	u32 can_id;
	u64 data;
};

struct virtcan_priv {
	struct virtio_device *vdev;
	struct virtqueue     *cvq;
	struct can_priv      *can;
	struct napi_struct    napi;

	struct clk           *clk_ipg;
	struct clk           *clk_per;

	/* Has control virtqueue */
	bool has_cvq;
};

static int virtcan_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	const struct virtcan_priv *priv = netdev_priv(dev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	u32 can_id;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(dev);

	can_put_echo_skb(skb, dev, 0);

	// TODO: Write to virtqueues

	return NETDEV_TX_OK;
}

static int virtcan_open(struct net_device *dev)
{
	struct virtcan_priv *priv = netdev_priv(dev);
	int err;

	err = open_candev(dev);
	if (err)
		return err;

	err = request_irq(dev->irq, virtcan_irq, IRQF_SHARED, dev->name, dev);
	if (err)
		goto out_close;

	err = virtcan_chip_control(dev, VIRTIO_CAN_CTRL_CHIP_START);
	if (err)
		goto out_free_irq;

	napi_enable(&priv->napi);
	netif_start_queue(dev);

	return 0;

out_free_irq:
	free_irq(dev->irq, dev);
out_close:
	close_candev(dev);

	return err;
}

static int virtcan_close(struct net_device *dev)
{
	struct virtcan_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	napi_disable(&priv->napi);
	virtcan_chip_control(dev, VIRTIO_CAN_CTRL_CHIP_STOP);

	free_irq(dev->irq, dev);
	clk_disable_unprepare(priv->clk_per);
	clk_disable_unprepare(priv->clk_ipg);

	close_candev(dev);

	return 0;
}

static const struct net_device_ops virtcan_netdev_ops = {
	.ndo_open       = virtcan_open,
	.ndo_stop       = virtcan_stop,
	.ndo_start_xmit = virtcan_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static bool virtcan_send_command(struct virtcan_priv *priv, u8 class, u8 cmd,
	struct scatterlist *out)
{
	struct scatterlist *sgs[4], hdr, stat;
	struct virtio_can_ctrl_hdr ctrl;
	virtio_can_ctrl_ack status = ~0;
	unsigned out_num = 0, tmp;

	/* Control queue is a needed for basic operation */
	BUG_ON(!virtio_has_feature(priv->vdev, VIRTIO_CAN_F_CTRL_VQ));

	ctrl = { .class = class, .cmd = cmd };
	/* Add header */
	sg_init_one(&hdr, &ctrl, sizeof(ctrl));
	sgs[out_num++] = &hdr;

	if (out)
		sgs[out_num++] = out;

	/* Add return status */
	sg_init_one(&stat, &status, sizeof(status));
	sgs[out_num] = &stat;

	BUG_ON(out_num + 1 > ARRAY_SIZE(sgs));
	virtqueue_add_sgs(priv->cvq, sgs, out_num, 1, priv, GFP_ATOMIC);

	if (unlikely(!virtqueue_kick(priv->cvq)))
		return status == VIRTIO_CAN_OK;

	/* Spin for a response, the kick causes an ioport write, trapping
	 * into the hypervisor, so the request should be handled immediately.
	 */
	while (!virtqueue_get_buf(vi->cvq, &tmp) &&
	       !virtqueue_is_broken(vi->cvq))
		cpu_relax();

	return status == VIRTIO_NET_OK;
}

static int virtcan_chip_control(struct virtcan_priv *priv, int op)
{
	int ret = 0;

	switch (op) {
	case VIRTIO_CAN_CTRL_CHIP_ENABLE:
	case VIRTIO_CAN_CTRL_CHIP_DISABLE:
	case VIRTIO_CAN_CTRL_CHIP_FREEZE:
	case VIRTIO_CAN_CTRL_CHIP_UNFREEZE:
	case VIRTIO_CAN_CTRL_CHIP_SOFTRESET:
		if (!virtcan_send_command(priv, VIRTIO_CAN_CTRL_CHIP, op, NULL))
			ret = -ETIMEDOUT;
		break;
	default:
		pr_debug("virtcan: Unknown chip control operation: %X\n", op);
		ret = -EBADRQC;
	}

	return ret;
}

static int register_virtcandev(struct virtio_device *vdev)
{
	struct net_device *dev = dev_get_drvdata(vdev->dev);
	u32 err;

	err = clk_prepare_enable(priv->clk_ipg);
	if (err)
		return err;

	err = clk_prepare_enable(priv->clk_per);
	if (err)
		goto out_disable_ipg;

	err = virtcan_chip_control(vdev->priv, VIRTIO_CAN_CTRL_CHIP_DISABLE);
	if (err)
		goto out_disable_per;

	// TODO: clocks settings

	err = virtcan_chip_control(vdev->priv, VIRTIO_CAN_CTRL_CHIP_ENABLE);
	if (err)
		goto out_chip_disable;

	// TODO: fifo settings. Maybe vqs ?

	err = register_candev(dev);

out_chip_disable:
	virtcan_chip_control(vdev->priv, VIRTIO_CAN_CTRL_CHIP_DISABLE);
out_disable_per:
	clk_disable_unprepare(priv->clk_per);
out_disable_ipg:
	clk_disable_unprepare(priv->clk_ipg);

	return err;
}

static void unregister_virtcandev(struct virtio_device *vdev)
{
	struct net_device *dev = dev_get_drvdata(vdev->dev);

	unregister_candev(dev);
}

// TODO: Get clocks (timings, ...) through virtio config options
static int virtcan_probe(struct virtio_device *vdev)
{
	struct net_device   *dev;
	struct virtcan_priv *priv;
	u32 clock_freq = 0;
	int err;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
		        __func__);
		return -EINVAL;
	}

	/* CAN device setup */
	dev = alloc_candev(sizeof(struct virtcan_priv), 1);
	if (!dev)
		return -ENOMEM;

	dev->netdev_ops = &virtcan_netdev_ops;
	dev->flags |= IFF_ECHO;

	/* Network device setup */
	priv = netdev_priv(dev);
	priv->can.clock.freq = clock_freq;

	netif_napi_add(dev, &priv->napi, virtcan_poll, VIRTCAN_NAPI_WEIGHT);

	if (virtio_has_feature(vdev, VIRTIO_CAN_F_CTRL_VQ))
		priv->has_cvq = true;

	err = register_virtcandev(vdev);
	if (err) {
		pr_debug("virtcan: registering netdev failed\n");
		goto failed_register;
	}

	virtio_device_ready(vdev);

	pr_debug("virtcan: registered device %s\n", dev->name);

	return 0;

failed_register:
	free_candev(dev);
	return err;
}

static void virtnet_remove(struct virtio_device *vdev)
{
	struct virtcan_priv *vi = vdev->priv;
	struct net_device *dev = dev_get_drvdata(vdev->dev);

	unregister_virtcandev(vdev);
	netif_napi_del(&vi->napi);
	free_netdev(dev);
}

#ifdef CONFIG_PM_SLEEP
static int virtcan_freeze(struct virtio_device *vdev)
{
	struct net_device   *dev  = dev_get_drvdata(vdev->dev);
	struct virtcan_priv *priv = vdev->priv;
	int err;

	err = virtcan_chip_control(priv, VIRTIO_CAN_CTRL_CHIP_DISABLE);
	if (err)
		return err;

	if (netif_running(dev)) {
		netif_stop_queue(dev);
		netif_device_detach(dev);
	}
	priv->can.state = CAN_STATE_SLEEPING;

	return 0;
}

static int virtcan_restore(struct virtio_device *vdev)
{
	struct net_device   *dev  = dev_get_drvdata(vdev->dev);
	struct virtcan_priv *priv = vdev->priv;

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	if (netif_running(dev)) {
		netif_device_attach(dev);
		netif_start_queue(dev);
	}

	return virtcan_chip_control(priv, VIRTIO_CAN_CTRL_CHIP_ENABLE);
}
#endif

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CAN, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_CAN_F_CTRL_VQ,
	VIRTIO_CAN_F_GUEST_CANFD, VIRTIO_CAN_F_HOST_CANFD,
};

static struct virtio_driver virtio_can_driver = {
	.feature_table      = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name        = KBUILD_MODNAME,
	.driver.owner       = THIS_MODULE,
	.id_table           = id_table,
	.probe              = virtcan_probe,
	.remove             = virtcan_remove,
	.config_changed     = virtcan_config_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze             = virtcan_freeze,
	.restore            = virtcan_restore,
#endif
};

module_virtio_driver(virtio_can_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio CAN bus driver");
MODULE_LICENSE("GPL");

